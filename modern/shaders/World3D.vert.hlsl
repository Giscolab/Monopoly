cbuffer SceneUniforms : register(b0, space1)
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
};

struct VertexInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

struct VertexOutput
{
    float4 position      : SV_Position;
    float3 normal        : TEXCOORD0;
    float2 uv            : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    const float4 localPosition = float4(input.position, 1.0f);
    output.position = mul(localPosition, worldViewProjection);
    output.worldPosition = mul(localPosition, world).xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    output.uv = input.uv;
    return output;
}
