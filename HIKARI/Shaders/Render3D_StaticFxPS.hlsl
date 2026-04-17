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
    float2 gObjectPadding;

    float4 gFxUser0;
    float4 gFxUser1;
    float4 gFxUser2;
    float4 gFxUser3;
};

cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir;
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gSpecularParams;
    float4 gPointLightPosRange[4];
    float4 gPointLightColorIntensity[4];
    float gDirectionalIntensity;
    float gAmbientIntensity;
    uint gPointLightCount;
    float gLightPadding;
};

Texture2D gBaseColorTex : register(t0);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position   : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS   : NORMAL;
    float2 uv         : TEXCOORD0;
};

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

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWS);
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

    float3 lit = albedo.rgb * (ambient + diffuse + specular + pointLightContribution);

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

    float3 edgeColor = float3(0.2, 0.9, 1.0) * edgeBand * edgeBoost;
    float3 finalColor = lit * edge + rimColor + edgeColor;

    return float4(saturate(finalColor), albedo.a);
}
