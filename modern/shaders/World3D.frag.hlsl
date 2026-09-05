cbuffer MaterialUniforms : register(b0, space3)
{
    float4 materialDiffuse;
    float4 sceneAmbient;
};

struct FragmentInput
{
    float4 position : SV_Position;
    float3 normal   : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
    // Source-faithful first lighting boundary: DISPLAY_UDBOARD_Initialize
    // installs mAmbientHalf = (0.53, 0.53, 0.53). Directional and spot
    // lights remain separate runtime work and are not fabricated here.
    return float4(materialDiffuse.rgb * sceneAmbient.rgb, materialDiffuse.a);
}
