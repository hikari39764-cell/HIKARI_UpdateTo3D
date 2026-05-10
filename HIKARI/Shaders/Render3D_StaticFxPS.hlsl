cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4 gBaseColor;
    uint gHasBaseColorTexture;
    uint gFxFlags;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float4 gEmissiveFactor;
    uint gHasNormalTexture;
    float gNormalScale;
    float2 gNormalPadding;
    uint gReceiveShadow;
    float3 gShadowObjectPadding;
    uint gHasEmissiveTexture;
    float3 gEmissivePadding;

    float4 gFxUser0;
    float4 gFxUser1;
    float4 gFxUser2;
    float4 gFxUser3;
};

static const uint MATERIAL_UNLIT = 1u << 0;
static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint MATERIAL_EMISSIVE = 1u << 2;

cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir;
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gSpecularParams;
    float4 gPointLightPosRange[8];
    float4 gPointLightColorIntensity[8];
    float gDirectionalIntensity;
    float gAmbientIntensity;
    uint gPointLightCount;
    float gLightPadding;
    float4 gFogColorDensity;
    float4 gFogParams;
};

cbuffer ShadowCB : register(b4)
{
    float4x4 gShadowLightViewProj;
    uint gShadowEnabled;
    float gShadowDepthBias;
    float gShadowNormalBias;
    float gShadowStrength;
    uint gShadowPcfEnabled;
    float gShadowPcfRadius;
    float gShadowTexelSizeX;
    float gShadowTexelSizeY;
};

Texture2D gBaseColorTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gShadowMap : register(t2);
Texture2D gEmissiveTex : register(t3);
SamplerState gLinearWrap : register(s0);
SamplerState gShadowSampler : register(s1);

struct PSInput
{
    float4 position   : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS   : NORMAL;
    float4 tangentWS  : TANGENT;
    float2 uv         : TEXCOORD0;
};

float3 ResolveShadingNormal(float3 normalWS, float4 tangentWS, float2 uv)
{
    float3 n = normalize(normalWS);
    if (gHasNormalTexture == 0)
    {
        return n;
    }

    float3 t = tangentWS.xyz;
    if (dot(t, t) < 1e-5f)
    {
        return n;
    }

    t = normalize(t);
    t = t - n * dot(n, t);
    if (dot(t, t) < 1e-5f)
    {
        return n;
    }
    t = normalize(t);
    float3 b = normalize(cross(n, t) * tangentWS.w);

    float3 normalTS = gNormalTex.Sample(gLinearWrap, uv).xyz * 2.0f - 1.0f;
    normalTS.xy *= gNormalScale;
    normalTS = normalize(normalTS);
    return normalize(normalTS.x * t + normalTS.y * b + normalTS.z * n);
}

float Hash31(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float Noise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);

    float n000 = Hash31(i + float3(0,0,0));
    float n100 = Hash31(i + float3(1,0,0));
    float n010 = Hash31(i + float3(0,1,0));
    float n110 = Hash31(i + float3(1,1,0));
    float n001 = Hash31(i + float3(0,0,1));
    float n101 = Hash31(i + float3(1,0,1));
    float n011 = Hash31(i + float3(0,1,1));
    float n111 = Hash31(i + float3(1,1,1));

    float3 u = f * f * (3.0 - 2.0 * f);

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);

    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);

    return lerp(nxy0, nxy1, u.z);
}

float3 AccumulatePointLight(float3 normalWS, float3 worldPosWS, float3 viewDir)
{
    float3 sum = 0.0f.xxx;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount) break;

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);

        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? toLight / dist : float3(0,1,0);

        float atten = saturate(1.0 - dist / range);
        atten *= atten;

        float ndotl = saturate(dot(normalWS, l));
        float3 h = normalize(l + viewDir);
        float spec = pow(saturate(dot(normalWS, h)), gSpecularParams.y);

        float3 color = gPointLightColorIntensity[i].rgb;
        float intensity = gPointLightColorIntensity[i].w;
        sum += color * (atten * intensity) * (ndotl + gSpecularParams.x * spec);
    }
    return sum;
}

float3 ResolveEmissive(float2 uv)
{
    float3 emissive = gEmissiveFactor.rgb;
    if (gHasEmissiveTexture != 0)
    {
        emissive *= gEmissiveTex.Sample(gLinearWrap, uv).rgb;
    }
    return emissive * gEmissiveFactor.a;
}

float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }

    float dist = length(gCameraPos.xyz - worldPosWS);
    float fogRange = max(0.001f, gFogParams.z - gFogParams.y);
    float fogFactor = saturate((dist - gFogParams.y) / fogRange);
    fogFactor = saturate(fogFactor * max(0.0f, gFogColorDensity.a) * dist);
    return lerp(color, gFogColorDensity.rgb, fogFactor);
}

float CompareShadowDepth(float2 uv, float currentDepth)
{
    float shadowDepth = gShadowMap.SampleLevel(gShadowSampler, uv, 0).r;
    return currentDepth <= shadowDepth ? 1.0f : 0.0f;
}

float SampleShadowPcf(float2 uv, float currentDepth)
{
    float visibility = CompareShadowDepth(uv, currentDepth);
    if (gShadowPcfEnabled == 0)
    {
        return visibility;
    }

    float2 texelSize = float2(gShadowTexelSizeX, gShadowTexelSizeY) * gShadowPcfRadius;
    float sum = 0.0f;
    sum += CompareShadowDepth(uv + texelSize * float2(-1.0f, -1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 0.0f, -1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 1.0f, -1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2(-1.0f,  0.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 0.0f,  0.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 1.0f,  0.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2(-1.0f,  1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 0.0f,  1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 1.0f,  1.0f), currentDepth);
    visibility = sum / 9.0f;
    return visibility;
}

float SampleDirectionalShadow(float3 worldPosWS, float3 geometricNormalWS)
{
    if (gShadowEnabled == 0 || gReceiveShadow == 0)
    {
        return 1.0f;
    }

    float3 biasedWorldPos = worldPosWS + geometricNormalWS * gShadowNormalBias;
    float4 lightClip = mul(gShadowLightViewProj, float4(biasedWorldPos, 1.0f));
    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 proj = lightClip.xyz / lightClip.w;
    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || proj.z < 0.0f || proj.z > 1.0f)
    {
        return 1.0f;
    }

    float currentDepth = proj.z - gShadowDepthBias;
    float visibility = SampleShadowPcf(uv, currentDepth);
    return lerp(1.0f - gShadowStrength, 1.0f, visibility);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 n = ResolveShadingNormal(input.normalWS, input.tangentWS, input.uv);
    float3 geometricNormal = normalize(input.normalWS);
    float3 l = normalize(gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
    float3 h = normalize(l + v);

    float ndotl = saturate(dot(n, l));
    float spec = pow(saturate(dot(n, h)), gSpecularParams.y);

    float3 ambient = gAmbientColor.rgb * gAmbientIntensity;
    float3 diffuse = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl);
    float3 specular = gDirectionalColor.rgb * (gDirectionalIntensity * gSpecularParams.x * spec);
    float3 pointLightContribution = AccumulatePointLight(n, input.worldPosWS, v);

    float4 albedo = gBaseColor;
    if (gHasBaseColorTexture != 0)
    {
        albedo *= gBaseColorTex.Sample(gLinearWrap, input.uv);
    }
    if ((gMaterialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < gAlphaCutoff)
    {
        discard;
    }

    float3 lit = albedo.rgb;
    if ((gMaterialFlags & MATERIAL_UNLIT) == 0)
    {
        lit = albedo.rgb * (ambient + (diffuse + specular + pointLightContribution) * SampleDirectionalShadow(input.worldPosWS, geometricNormal));
    }
    if ((gMaterialFlags & MATERIAL_EMISSIVE) != 0)
    {
        lit += ResolveEmissive(input.uv);
    }
    lit = ApplyFog(lit, input.worldPosWS);

    float rimStrength = gFxUser0.x;
    float rimPower    = max(gFxUser0.y, 0.01);
    float dissolveAmt = saturate(gFxUser0.z);
    float edgeWidth   = max(gFxUser0.w, 0.0001);

    float pulseSpeed  = gFxUser1.x;
    float edgeBoost   = gFxUser1.y;
    float noiseScale  = max(gFxUser1.z, 0.0001);

    float pulse = 0.5 + 0.5 * sin(pulseSpeed);
    float rim = pow(1.0 - saturate(dot(n, v)), rimPower);
    float3 rimColor = float3(0.15, 0.75, 1.0) * rim * rimStrength * (0.75 + pulse * 0.25);

    float noise = Noise3D(input.worldPosWS * noiseScale + float3(0.0, 0.0, 0.0));
    float cutoff = dissolveAmt;
    float edge = smoothstep(cutoff, cutoff + edgeWidth, noise);
    float edgeBand = smoothstep(cutoff - edgeWidth, cutoff, noise) - smoothstep(cutoff, cutoff + edgeWidth, noise);

    if (noise < cutoff)
    {
        discard;
    }

    float3 edgeColor = float3(0.4, 0.5, 1.0) * edgeBand * edgeBoost;
    float3 finalColor = lit * edge + rimColor + edgeColor;

    return float4(finalColor, albedo.a);
}
