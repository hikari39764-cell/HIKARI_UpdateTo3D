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

float3 HikariEvaluateAmbientIblApprox(
    float3 baseColor,
    float metallic,
    float roughness,
    float occlusion,
    float screenAo,
    float3 n,
    float3 v)
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
            v);
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
            float probeWeight = saturate(1.0f - dist / probeRadius);
            probeWeight = probeWeight * probeWeight * (3.0f - 2.0f * probeWeight);
            probeWeight *= max(0.0f, gReflectionProbeSpecularIntensity);

            float probeMipCount = max(1.0f, gReflectionProbeMipCount);
            float probeMip = roughness * (probeMipCount - 1.0f);
            float3 probePrefiltered = gReflectionProbePrefilteredTex.SampleLevel(gLinearWrap, r, probeMip).rgb;
            float3 probeSpecular = probePrefiltered * F;
            if (gReflectionProbeHasBrdfLut > 0.5f)
            {
                float2 brdf = gIblBrdfLutTex.Sample(gLinearWrap, float2(ndotv, roughness)).rg;
                probeSpecular = probePrefiltered * (F * brdf.x + brdf.y);
            }

            // Sky IBL と local probe は加算ではなく正規化 blend にする。
            specular = (skySpecular + probeSpecular * probeWeight) / max(1.0f + probeWeight, 0.0001f);
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
