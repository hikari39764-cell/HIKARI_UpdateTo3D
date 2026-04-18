Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

cbuffer CommonParams : register(b0)
{
    float time;
    float deltaTime;
    float combo;
    float intensity;

    float resolutionX;
    float resolutionY;
    float pad0;
    float pad1;

    float4 user[16];
};

struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return frac(p.x * p.y);
}

float Noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));

    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float4 main(PS_IN i) : SV_TARGET
{
    float2 uv = i.uv;
    float4 src = gTex.Sample(gSamp, uv);

    float softness = max(0.0001, user[0].x);
    float noiseScale = max(0.0001, user[0].y);

    float progress = saturate(user[14].x);
    float isTransitionIn = user[14].y;

    float n = Noise(uv * noiseScale + time * 0.13);

    float threshold = (isTransitionIn > 0.5) ? (1.0 - progress) : progress;
    float visible = smoothstep(threshold - softness, threshold + softness, n);

    if (isTransitionIn <= 0.5)
    {
        visible = 1.0 - visible;
    }

    return float4(src.rgb * visible, src.a);
}
