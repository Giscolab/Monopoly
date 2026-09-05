cbuffer MaterialUniforms : register(b0, space3)
{
    float4 materialDiffuse;
    float4 sceneAmbient;
};

Texture2D legacyTexture : register(t0, space2);
SamplerState legacySampler : register(s0, space2);

struct PixelInput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main(PixelInput input) : SV_Target0
{
    // The retail D3D7 path does not override stage-0 COLOROP after device
    // initialization. Preserve the standard texture*diffuse modulation and
    // the ambient factor already carried by the modern renderer.
    const float4 texel = legacyTexture.Sample(legacySampler, input.uv);
    return float4(
        texel.rgb * materialDiffuse.rgb * sceneAmbient.rgb,
        texel.a * materialDiffuse.a);
}
