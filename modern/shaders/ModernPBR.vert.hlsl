cbuffer SceneUniforms : register(b0, space1)
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
};

struct VertexInput
{
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    const float4 localPosition = float4(input.position, 1.0f);
    output.position = mul(localPosition, worldViewProjection);
    output.worldPosition = mul(localPosition, world).xyz;
    // Cofactor rows form inverse-transpose for row-vector transforms.
    const float3 a = world[0].xyz;
    const float3 b = world[1].xyz;
    const float3 c = world[2].xyz;
    const float determinant = dot(a, cross(b, c));
    const float3x3 cofactor = float3x3(cross(b, c), cross(c, a), cross(a, b));
    output.normal = abs(determinant) > 0.00000001f
        ? mul(input.normal, cofactor) / determinant
        : mul(input.normal, (float3x3)world);
    output.uv = input.uv;
    const float3 transformedTangent = mul(input.tangent.xyz, (float3x3)world);
    const float3 n = normalize(output.normal);
    const float3 tangent = transformedTangent - n * dot(n, transformedTangent);
    output.tangent = float4(tangent, input.tangent.w * (determinant < 0.0f ? -1.0f : 1.0f));
    return output;
}
