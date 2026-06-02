#ifndef HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI
#define HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI

float3 HikariRotateSkyYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);

    return float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c
    );
}

float3 HikariEvaluateSkyApprox(float3 dir)
{
    dir = normalize(HikariRotateSkyYaw(dir, gSkyYaw));

    float y = saturate(dir.y * 0.5f + 0.5f);

    float3 upper = lerp(gSkyHorizonColor, gSkyZenithColor, y);
    float3 lower = lerp(gSkyGroundColor, gSkyHorizonColor, y);
    float3 color = (dir.y >= 0.0f) ? upper : lower;

    float horizon = pow(
        saturate(1.0f - abs(dir.y)),
        max(0.01f, gSkyHorizonPower)
    );

    color = lerp(color, gSkyHorizonColor, horizon * 0.25f);
    color *= max(0.0f, gSkyExposure);

    return color;
}

float3 HikariSampleSkyEnvironment(float3 dir)
{
    float3 sky = HikariEvaluateSkyApprox(dir);

    uint mode = (uint)(gSkyMode + 0.5f);

    if (mode == 2u && gSkyHasCubemap > 0.5f)
    {
        float3 cubeDir = normalize(HikariRotateSkyYaw(dir, gSkyYaw));
        sky = gSkyCube.Sample(gLinearWrap, cubeDir).rgb;
        sky *= max(0.0f, gSkyExposure);
    }

    return sky;
}

uint HikariLightProbeIndex(uint x, uint y, uint z)
{
    return z * gLightProbeCountX * gLightProbeCountY + y * gLightProbeCountX + x;
}

float3 HikariEvaluateLightProbeSh9(uint probeIndex, float3 n)
{
    uint baseIndex = probeIndex * 9u;
    float x = n.x;
    float y = n.y;
    float z = n.z;

    float b0 = 0.282095f;
    float b1 = 0.488603f * y;
    float b2 = 0.488603f * z;
    float b3 = 0.488603f * x;
    float b4 = 1.092548f * x * y;
    float b5 = 1.092548f * y * z;
    float b6 = 0.315392f * (3.0f * z * z - 1.0f);
    float b7 = 1.092548f * x * z;
    float b8 = 0.546274f * (x * x - y * y);

    const float c0 = 3.14159265f;
    const float c1 = 2.09439510f;
    const float c2 = 0.78539816f;

    float3 result = 0.0f.xxx;
    result += gLightProbeSh[baseIndex + 0u].rgb * (b0 * c0);
    result += gLightProbeSh[baseIndex + 1u].rgb * (b1 * c1);
    result += gLightProbeSh[baseIndex + 2u].rgb * (b2 * c1);
    result += gLightProbeSh[baseIndex + 3u].rgb * (b3 * c1);
    result += gLightProbeSh[baseIndex + 4u].rgb * (b4 * c2);
    result += gLightProbeSh[baseIndex + 5u].rgb * (b5 * c2);
    result += gLightProbeSh[baseIndex + 6u].rgb * (b6 * c2);
    result += gLightProbeSh[baseIndex + 7u].rgb * (b7 * c2);
    result += gLightProbeSh[baseIndex + 8u].rgb * (b8 * c2);
    return max(result, 0.0f.xxx);
}

float3 HikariSampleLightProbeCorner(uint3 cell, float3 n)
{
    uint index = HikariLightProbeIndex(cell.x, cell.y, cell.z);
    if (index >= gLightProbeProbeCount)
    {
        return 0.0f.xxx;
    }
    return HikariEvaluateLightProbeSh9(index, n);
}

float3 HikariEvaluateLightProbeVolumeDiffuse(float3 worldPos, float3 n)
{
    if (gLightProbeEnabled < 0.5f ||
        gLightProbeProbeCount == 0u ||
        gLightProbeCountX < 2u ||
        gLightProbeCountY < 1u ||
        gLightProbeCountZ < 2u)
    {
        return 0.0f.xxx;
    }

    float3 gridCoord = (worldPos - gLightProbeOrigin) / max(gLightProbeSpacing, float3(0.0001f, 0.0001f, 0.0001f));
    gridCoord.x = clamp(gridCoord.x, 0.0f, (float)(gLightProbeCountX - 1u));
    gridCoord.y = (gLightProbeCountY > 1u)
        ? clamp(gridCoord.y, 0.0f, (float)(gLightProbeCountY - 1u))
        : 0.0f;
    gridCoord.z = clamp(gridCoord.z, 0.0f, (float)(gLightProbeCountZ - 1u));

    uint x0 = (uint)min(floor(gridCoord.x), (float)(gLightProbeCountX - 2u));
    uint y0 = (gLightProbeCountY > 1u)
        ? (uint)min(floor(gridCoord.y), (float)(gLightProbeCountY - 2u))
        : 0u;
    uint z0 = (uint)min(floor(gridCoord.z), (float)(gLightProbeCountZ - 2u));

    uint x1 = min(x0 + 1u, gLightProbeCountX - 1u);
    uint y1 = (gLightProbeCountY > 1u) ? min(y0 + 1u, gLightProbeCountY - 1u) : y0;
    uint z1 = min(z0 + 1u, gLightProbeCountZ - 1u);

    float3 f = float3(
        saturate(gridCoord.x - (float)x0),
        (gLightProbeCountY > 1u) ? saturate(gridCoord.y - (float)y0) : 0.0f,
        saturate(gridCoord.z - (float)z0));

    float3 c000 = HikariSampleLightProbeCorner(uint3(x0, y0, z0), n);
    float3 c100 = HikariSampleLightProbeCorner(uint3(x1, y0, z0), n);
    float3 c010 = HikariSampleLightProbeCorner(uint3(x0, y1, z0), n);
    float3 c110 = HikariSampleLightProbeCorner(uint3(x1, y1, z0), n);
    float3 c001 = HikariSampleLightProbeCorner(uint3(x0, y0, z1), n);
    float3 c101 = HikariSampleLightProbeCorner(uint3(x1, y0, z1), n);
    float3 c011 = HikariSampleLightProbeCorner(uint3(x0, y1, z1), n);
    float3 c111 = HikariSampleLightProbeCorner(uint3(x1, y1, z1), n);

    float3 cx00 = lerp(c000, c100, f.x);
    float3 cx10 = lerp(c010, c110, f.x);
    float3 cx01 = lerp(c001, c101, f.x);
    float3 cx11 = lerp(c011, c111, f.x);
    float3 cxy0 = lerp(cx00, cx10, f.y);
    float3 cxy1 = lerp(cx01, cx11, f.y);
    return lerp(cxy0, cxy1, f.z);
}

float3 HikariBlendLightProbeDiffuse(float3 fallbackDiffuse, float3 worldPos, float3 n)
{
    if (gLightProbeEnabled < 0.5f || gLightProbeIntensity <= 0.0001f)
    {
        return fallbackDiffuse;
    }

    float3 localDiffuse = HikariEvaluateLightProbeVolumeDiffuse(worldPos, n);
    return lerp(fallbackDiffuse, localDiffuse, saturate(gLightProbeIntensity));
}

float3 HikariEvaluateAmbientIblApprox(
    float3 baseColor,
    float metallic,
    float roughness,
    float occlusion,
    float screenAo,
    float3 n,
    float3 v,
    float3 worldPos)
{
    float3 F0 = lerp(0.04f.xxx, baseColor, metallic);
    float ndotv = saturate(dot(n, v));
    float3 F = HikariFresnelSchlick(ndotv, F0);

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);

    float3 diffuseAmbient = gAmbientColor.rgb * gAmbientIntensity;

    if (gSkyAmbientFromSky > 0.0001f)
    {
        float3 skyDiffuse =
            lerp(gSkyGroundColor, gSkyHorizonColor, saturate(n.y * 0.5f + 0.5f));
        skyDiffuse *= gSkyExposure * gSkyAmbientFromSky;
        diffuseAmbient = lerp(diffuseAmbient, skyDiffuse, saturate(gSkyAmbientFromSky));
    }

    diffuseAmbient = HikariBlendLightProbeDiffuse(diffuseAmbient, worldPos, n);
    float3 diffuse = kD * baseColor * diffuseAmbient;

    float3 r = normalize(reflect(-v, n));
    uint skyMode = (uint)(gSkyMode + 0.5f);
    if (!(skyMode == 2u && gSkyHasCubemap > 0.5f))
    {
        // Gradient fallback だけ上下反転を抑える。
        r.y = abs(r.y);
    }

    float3 specEnv = HikariSampleSkyEnvironment(r);
    specEnv *= max(0.0f, gSkyReflectionIntensity);

    // Approximation until prefiltered specular IBL is implemented.
    // Use the same factor in all material shaders to keep visuals consistent.
    float roughnessFade = 1.0f - saturate(roughness * 0.85f);
    float3 specular = specEnv * F * roughnessFade;

    float materialAo = saturate(occlusion);
    float ssao = saturate(screenAo);
    float diffuseAo = materialAo * lerp(1.0f, ssao, saturate(gSsaoDiffuseStrength));
    float specularAo = lerp(1.0f, materialAo * ssao, saturate(roughness * roughness) * saturate(gSsaoSpecularStrength));
    return diffuse * diffuseAo + specular * specularAo;
}

float3 HikariEvaluateAmbientIbl(
    float3 baseColor,
    float metallic,
    float roughness,
    float occlusion,
    float screenAo,
    float3 n,
    float3 v,
    float3 worldPos)
{
    float3 result = 0.0f.xxx;

    bool hasLocalProbe = (gReflectionProbeEnabled > 0.5f && gReflectionProbeHasPrefiltered > 0.5f);
    if (gIblHasIrradiance < 0.5f && gIblHasPrefiltered < 0.5f && !hasLocalProbe)
    {
        result = HikariEvaluateAmbientIblApprox(
            baseColor,
            metallic,
            roughness,
            occlusion,
            screenAo,
            n,
            v,
            worldPos);
    }
    else
    {
        float3 F0 = lerp(0.04f.xxx, baseColor, metallic);
        float ndotv = saturate(dot(n, v));
        float3 F = HikariFresnelSchlick(ndotv, F0);

        float3 kS = F;
        float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);

        float3 diffuseIbl = 0.0f.xxx;
        if (gIblHasIrradiance > 0.5f)
        {
            diffuseIbl = gIblIrradianceTex.Sample(gLinearWrap, n).rgb;
        }
        else
        {
            diffuseIbl = gAmbientColor.rgb * gAmbientIntensity;
        }

        diffuseIbl = HikariBlendLightProbeDiffuse(diffuseIbl, worldPos, n);
        float3 diffuse = kD * baseColor * diffuseIbl;

        float3 skySpecular = 0.0f.xxx;
        if (gIblHasPrefiltered > 0.5f)
        {
            float3 r = normalize(reflect(-v, n));
            float mipCount = max(1.0f, gIblPrefilteredMipCount);
            float mip = roughness * (mipCount - 1.0f);
            float3 prefiltered = gIblPrefilteredTex.SampleLevel(gLinearWrap, r, mip).rgb;

            if (gIblHasBrdfLut > 0.5f)
            {
                float2 brdf = gIblBrdfLutTex.Sample(gLinearWrap, float2(ndotv, roughness)).rg;
                skySpecular = prefiltered * (F * brdf.x + brdf.y);
            }
            else
            {
                skySpecular = prefiltered * F;
            }
        }
        else
        {
            float3 r = normalize(reflect(-v, n));
            float roughnessFade = 1.0f - saturate(roughness * 0.85f);
            skySpecular = HikariSampleSkyEnvironment(r) * F * roughnessFade * max(0.0f, gSkyReflectionIntensity);
        }

        float3 specular = skySpecular;
        if (hasLocalProbe)
        {
            float3 r = normalize(reflect(-v, n));
            float probeRadius = max(0.001f, gReflectionProbeRadius);
            float dist = length(worldPos - gReflectionProbePosition);
            float probeInfluence = saturate(1.0f - dist / probeRadius);
            probeInfluence = probeInfluence * probeInfluence * (3.0f - 2.0f * probeInfluence);
            probeInfluence = saturate(probeInfluence * max(0.0f, gReflectionProbeSpecularIntensity));

            float probeMipCount = max(1.0f, gReflectionProbeMipCount);
            float probeMip = roughness * (probeMipCount - 1.0f);
            float3 probePrefiltered = gReflectionProbePrefilteredTex.SampleLevel(gLinearWrap, r, probeMip).rgb;
            float3 probeSpecular = probePrefiltered * F;
            if (gReflectionProbeHasBrdfLut > 0.5f)
            {
                float2 brdf = gIblBrdfLutTex.Sample(gLinearWrap, float2(ndotv, roughness)).rg;
                probeSpecular = probePrefiltered * (F * brdf.x + brdf.y);
            }

            // Local probe は加算や平均ではなく、影響範囲内で Sky IBL を置き換える。
            specular = lerp(skySpecular, probeSpecular, probeInfluence);
        }

        float materialAo = saturate(occlusion);
        float ssao = saturate(screenAo);
        float diffuseAo = materialAo * lerp(1.0f, ssao, saturate(gSsaoDiffuseStrength));
        float specularAo = lerp(1.0f, materialAo * ssao, saturate(roughness * roughness) * saturate(gSsaoSpecularStrength));
        result = diffuse * diffuseAo + specular * specularAo;
    }

    return result;
}

#endif
