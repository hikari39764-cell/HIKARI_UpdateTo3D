cbuffer TemporalTaaCB : register(b0)
{
    float4 gScreenParams; // xy: render size, zw: inverse render size
    float4 gTaaParams;    // x: history valid, y: history weight, z: reserved, w: clip gamma
    float4 gRejectParams; // x: depth threshold, y: luma threshold, z: sharpness
};

Texture2D<float4> gCurrentColor : register(t0);
Texture2D<float4> gHistoryColor : register(t1);
Texture2D<float2> gMotionVectors : register(t2);
Texture2D<float> gSceneDepth : register(t3);
Texture2D<float> gHistoryDepth : register(t4);
SamplerState gLinearClamp : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PSOut
{
    float4 color : SV_TARGET0;
    float depth : SV_TARGET1;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 RGBToYCoCg(float3 color)
{
    return float3(
        dot(color, float3(0.25f, 0.5f, 0.25f)),
        color.r - color.b,
        color.g - dot(color.rb, float2(0.5f, 0.5f)));
}

float3 YCoCgToRGB(float3 color)
{
    float y = color.x;
    float co = color.y;
    float cg = color.z;
    float chromaBase = y - cg * 0.5f;
    return float3(
        chromaBase + co * 0.5f,
        y + cg * 0.5f,
        chromaBase - co * 0.5f);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 EncodeTemporalColor(float3 color)
{
    color = max(color, float3(0.0f, 0.0f, 0.0f));
    return color / (1.0f + Luminance(color));
}

float3 DecodeTemporalColor(float3 color)
{
    return max(color / max(1.0f - Luminance(color), 1e-4f), float3(0.0f, 0.0f, 0.0f));
}

float CatmullRomWeight(float x)
{
    x = abs(x);
    if (x <= 1.0f)
    {
        return ((1.5f * x - 2.5f) * x) * x + 1.0f;
    }
    if (x < 2.0f)
    {
        return (((-0.5f * x + 2.5f) * x - 4.0f) * x) + 2.0f;
    }
    return 0.0f;
}

float4 LoadClampedColor(int2 pixel)
{
    int2 size = max(int2(gScreenParams.xy), int2(1, 1));
    int2 p = clamp(pixel, int2(0, 0), size - 1);
    return gCurrentColor.Load(int3(p, 0));
}

float4 LoadClampedHistory(int2 pixel)
{
    int2 size = max(int2(gScreenParams.xy), int2(1, 1));
    int2 p = clamp(pixel, int2(0, 0), size - 1);
    return gHistoryColor.Load(int3(p, 0));
}

float4 SampleHistoryCatmullRom(float2 uv)
{
    float2 texelCoord = uv * gScreenParams.xy - 0.5f;
    int2 basePixel = int2(floor(texelCoord));
    float2 f = texelCoord - float2(basePixel);

    float4 sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        float wy = CatmullRomWeight(float(y) - f.y);
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            float wx = CatmullRomWeight(float(x) - f.x);
            float w = wx * wy;
            sum += LoadClampedHistory(basePixel + int2(x, y)) * w;
            weightSum += w;
        }
    }
    return sum / max(weightSum, 1e-5f);
}

float3 ClipHistoryToNeighborhood(uint2 pixel, float3 historyEncoded)
{
    int2 basePixel = int2(pixel);
    float3 mean = 0.0f;
    float3 meanSq = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 c = RGBToYCoCg(EncodeTemporalColor(LoadClampedColor(basePixel + int2(x, y)).rgb));
            mean += c;
            meanSq += c * c;
        }
    }

    mean *= 1.0f / 9.0f;
    meanSq *= 1.0f / 9.0f;
    float3 sigma = sqrt(max(meanSq - mean * mean, float3(0.0f, 0.0f, 0.0f)));
    float gamma = max(gTaaParams.w, 0.0f);
    float3 historyYCoCg = RGBToYCoCg(historyEncoded);
    historyYCoCg = clamp(historyYCoCg, mean - sigma * gamma, mean + sigma * gamma);
    return max(YCoCgToRGB(historyYCoCg), float3(0.0f, 0.0f, 0.0f));
}

float3 SpatialFilteredCurrent(uint2 pixel)
{
    int2 basePixel = int2(pixel);
    float3 sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float weight =
                (x == 0 && y == 0) ? 4.0f :
                ((x == 0 || y == 0) ? 2.0f : 1.0f);
            sum += EncodeTemporalColor(LoadClampedColor(basePixel + int2(x, y)).rgb) * weight;
            weightSum += weight;
        }
    }

    return sum / max(weightSum, 1e-5f);
}

void CurrentNeighborhoodBounds(uint2 pixel, out float3 minColor, out float3 maxColor)
{
    int2 basePixel = int2(pixel);
    minColor = float3(65504.0f, 65504.0f, 65504.0f);
    maxColor = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 c = EncodeTemporalColor(LoadClampedColor(basePixel + int2(x, y)).rgb);
            minColor = min(minColor, c);
            maxColor = max(maxColor, c);
        }
    }
}

float3 CurrentFallback(uint2 pixel, float3 currentEncoded)
{
    const float fallbackFilterStrength = 0.18f;
    return lerp(currentEncoded, SpatialFilteredCurrent(pixel), fallbackFilterStrength);
}

float3 ApplyTemporalSharpen(uint2 pixel, float3 resolvedEncoded)
{
    float sharpness = saturate(gRejectParams.z);
    if (sharpness <= 0.0f)
    {
        return resolvedEncoded;
    }

    int2 basePixel = int2(pixel);
    float3 center = EncodeTemporalColor(LoadClampedColor(basePixel).rgb);
    float3 crossAverage =
        (EncodeTemporalColor(LoadClampedColor(basePixel + int2(-1, 0)).rgb) +
         EncodeTemporalColor(LoadClampedColor(basePixel + int2(1, 0)).rgb) +
         EncodeTemporalColor(LoadClampedColor(basePixel + int2(0, -1)).rgb) +
         EncodeTemporalColor(LoadClampedColor(basePixel + int2(0, 1)).rgb)) * 0.25f;

    float3 minColor;
    float3 maxColor;
    CurrentNeighborhoodBounds(pixel, minColor, maxColor);
    return clamp(resolvedEncoded + (center - crossAverage) * sharpness, minColor, maxColor);
}

float LoadClampedDepth(int2 pixel)
{
    int2 size = max(int2(gScreenParams.xy), int2(1, 1));
    int2 p = clamp(pixel, int2(0, 0), size - 1);
    return gSceneDepth.Load(int3(p, 0)).r;
}

float HistoryDepthReject(uint2 pixel, float2 previousUv)
{
    int2 size = max(int2(gScreenParams.xy), int2(1, 1));
    int2 basePixel = int2(pixel);
    float minDepth = 1.0f;
    float maxDepth = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float d = LoadClampedDepth(basePixel + int2(x, y));
            minDepth = min(minDepth, d);
            maxDepth = max(maxDepth, d);
        }
    }

    int2 previousPixel = clamp(int2(previousUv * gScreenParams.xy), int2(0, 0), size - 1);
    float previousDepth = gHistoryDepth.Load(int3(previousPixel, 0)).r;
    if (previousDepth >= 0.99999f)
    {
        return 1.0f;
    }

    float depthThreshold = max(gRejectParams.x, 0.0001f);
    float outsideRange =
        max(max(minDepth - previousDepth, previousDepth - maxDepth), 0.0f);
    return saturate(outsideRange / (depthThreshold * 8.0f));
}

float HistoryConfidence(uint2 pixel, float2 previousUv, float3 current, float3 history, float2 motionPixels)
{
    float depthReject = HistoryDepthReject(pixel, previousUv);

    float currentLuma = Luminance(current);
    float historyLuma = Luminance(history);
    float lumaBase = max(max(currentLuma, historyLuma), 0.25f);
    float lumaReject = saturate(
        (abs(currentLuma - historyLuma) / lumaBase) /
        max(gRejectParams.y, 0.05f));

    float confidence = 1.0f;
    confidence *= 1.0f - depthReject;
    confidence *= 1.0f - lumaReject;
    return saturate(confidence);
}

float ResolveHistoryWeight(float baseHistoryWeight, float historyConfidence, float2 motionPixels)
{
    const float stationaryHistoryBoost = 0.04f;
    const float maxStationaryHistoryWeight = 0.965f;
    float motionLength = length(motionPixels);
    float movingBlend = smoothstep(1.25f, 5.0f, motionLength);
    float stationaryHistoryWeight =
        max(baseHistoryWeight, min(maxStationaryHistoryWeight, baseHistoryWeight + stationaryHistoryBoost));
    float adaptiveWeight =
        lerp(stationaryHistoryWeight, baseHistoryWeight, movingBlend);
    return saturate(adaptiveWeight) * historyConfidence;
}

PSOut PSMain(VSOut input)
{
    uint2 size = max(uint2(gScreenParams.xy), uint2(1u, 1u));
    uint2 pixel = min(uint2(input.position.xy), size - 1u);
    float4 current = gCurrentColor.Load(int3(pixel, 0));
    float currentDepth = LoadClampedDepth(int2(pixel));
    float3 currentEncoded = EncodeTemporalColor(current.rgb);

    PSOut output;
    output.depth = currentDepth;

    if (gTaaParams.x < 0.5f)
    {
        output.color = current;
        return output;
    }

    float2 motionPixels = gMotionVectors.Load(int3(pixel, 0));
    float2 previousUv = input.uv - motionPixels * gScreenParams.zw;
    if (any(previousUv < 0.0f) || any(previousUv > 1.0f))
    {
        output.color = float4(DecodeTemporalColor(CurrentFallback(pixel, currentEncoded)), current.a);
        return output;
    }

    float4 history = SampleHistoryCatmullRom(previousUv);
    float3 historyEncoded = ClipHistoryToNeighborhood(pixel, EncodeTemporalColor(history.rgb));

    float historyConfidence =
        HistoryConfidence(pixel, previousUv, currentEncoded, historyEncoded, motionPixels);
    float historyWeight =
        ResolveHistoryWeight(saturate(gTaaParams.y), historyConfidence, motionPixels);

    float3 currentFallback = CurrentFallback(pixel, currentEncoded);
    float3 currentResolve = lerp(currentFallback, currentEncoded, historyConfidence);
    float3 resolvedEncoded = lerp(currentResolve, historyEncoded, historyWeight);
    resolvedEncoded = ApplyTemporalSharpen(pixel, resolvedEncoded);
    output.color = float4(DecodeTemporalColor(resolvedEncoded), current.a);
    return output;
}
