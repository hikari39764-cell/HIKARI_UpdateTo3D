cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4 gBaseColorFactor;
    float4 gEmissiveFactor;
    float gNormalScale;
    float gOcclusionStrength;
    float gMetallicFactor;
    float gRoughnessFactor;
    uint gHasBaseColorTexture;
    uint gHasNormalTexture;
    uint gHasOrmTexture;
    uint gHasEmissiveTexture;
    uint gAlphaMode;
    float gAlphaCutoff;
    uint gFxFlags;
    float gObjectPadding;
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
Texture2D gNormalTex : register(t1);
Texture2D gOrmTex : register(t2);
Texture2D gEmissiveTex : register(t3);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
    float3 bitangentWS : TEXCOORD4;
    float2 uv : TEXCOORD0;
};

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
    float3 n = normalize(input.normalWS);
    if (gHasNormalTexture != 0)
    {
        float3 tn = gNormalTex.Sample(gLinearWrap, input.uv).xyz * 2.0f - 1.0f;
        tn.xy *= gNormalScale;
        float3x3 tbn = float3x3(normalize(input.tangentWS), normalize(input.bitangentWS), normalize(input.normalWS));
        n = normalize(mul(tbn, tn));
    }
    float3 l = normalize(gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
    float3 h = normalize(l + v);

    float ndotl = saturate(dot(n, l));
    float spec = pow(saturate(dot(n, h)), gSpecularParams.y);

    float3 ambient = gAmbientColor.rgb * gAmbientIntensity;
    float3 diffuse = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl);
    float3 specular = gDirectionalColor.rgb * (gDirectionalIntensity * gSpecularParams.x * spec);
    float3 pointLightContribution = AccumulatePointLight(n, input.worldPosWS, v);

    float4 albedo = gBaseColorFactor;
    if (gHasBaseColorTexture != 0)
    {
        albedo *= gBaseColorTex.Sample(gLinearWrap, input.uv);
    }
    if (gAlphaMode == 1)
    {
        clip(albedo.a - gAlphaCutoff);
    }

    float ao = 1.0f;
    float metallic = gMetallicFactor;
    float roughness = gRoughnessFactor;
    if (gHasOrmTexture != 0)
    {
        float3 orm = gOrmTex.Sample(gLinearWrap, input.uv).rgb;
        ao = lerp(1.0f, orm.r, saturate(gOcclusionStrength));
        roughness *= orm.g;
        metallic *= orm.b;
    }
    float3 emissive = gEmissiveFactor.rgb;
    if (gHasEmissiveTexture != 0)
    {
        emissive *= gEmissiveTex.Sample(gLinearWrap, input.uv).rgb;
    }

    float roughSpecScale = lerp(1.0f, 0.25f, saturate(roughness));
    float3 lit = ambient * ao + diffuse + (specular + pointLightContribution) * roughSpecScale;
    lit *= (1.0f - metallic * 0.25f);
    return float4(albedo.rgb * lit + emissive, albedo.a);
}
