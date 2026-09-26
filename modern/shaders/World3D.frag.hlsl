cbuffer MaterialUniforms : register(b0, space3)
{
    float4 materialDiffuse;
};

Texture2D legacyTexture : register(t0, space2);
SamplerState legacySampler : register(s0, space2);

struct PixelInput
{
    float4 position      : SV_Position;
    float3 color         : TEXCOORD0;
    float2 uv            : TEXCOORD1;
};

float4 main(PixelInput input) : SV_Target0
{
    const float4 texel = legacyTexture.Sample(legacySampler, input.uv);
    return float4(
        texel.rgb * input.color,
        texel.a * materialDiffuse.a);
}
