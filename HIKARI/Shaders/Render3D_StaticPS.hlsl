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
};

Texture2D gBaseColorTex : register(t0);
Texture2D gNormalTex : register(t1);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
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

float3 AccumulatePointLight(float3 normalWS, float3 worldPosWS, float3 viewDir)
{
    float3 sum = 0.0f.xxx;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount)
        {
            break;
        }

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);
        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? (toLight / dist) : float3(0.0f, 1.0f, 0.0f);

        float atten = saturate(1.0f - dist / range);
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
    float3 n = ResolveShadingNormal(input.normalWS, input.tangentWS, input.uv);
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
    if ((gMaterialFlags & MATERIAL_UNLIT) != 0)
    {
        float3 unlitColor = albedo.rgb;
        if ((gMaterialFlags & MATERIAL_EMISSIVE) != 0)
        {
            unlitColor += gEmissiveFactor.rgb * gEmissiveFactor.a;
        }
        return float4(unlitColor, albedo.a);
    }

    float3 lit = ambient + diffuse + specular + pointLightContribution;
    float3 finalColor = albedo.rgb * lit;
    if ((gMaterialFlags & MATERIAL_EMISSIVE) != 0)
    {
        finalColor += gEmissiveFactor.rgb * gEmissiveFactor.a;
    }
    return float4(finalColor, albedo.a);
}
