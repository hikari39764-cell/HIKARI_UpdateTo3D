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

    float3 r = reflect(-v, n);
    r.y = abs(r.y);

    float3 specEnv = HikariSampleSkyEnvironment(r);
    specEnv *= max(0.0f, gSkyReflectionIntensity);

    // Approximation until prefiltered specular IBL is implemented.
    // Use the same factor in all material shaders to keep visuals consistent.
    float roughnessFade = 1.0f - saturate(roughness * 0.85f);
    float3 specular = specEnv * F * roughnessFade;

    return (diffuse + specular) * occlusion;
}

#endif
