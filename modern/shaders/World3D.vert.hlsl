cbuffer SceneUniforms : register(b0, space1)
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
    float4 materialDiffuse;
    float4 sceneAmbient;
    float4 boardReflectionColorEnabled;
    float4 boardReflectionDirection;
    float4 sunColorEnabled;
    float4 sunDirection;
    float4 spotlightColorEnabled;
    float4 spotlightPositionRange;
    float4 spotlightDirectionFalloff;
    float4 spotlightAttenuationTheta;
    float4 spotlightPhi;
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
    float3 color         : TEXCOORD0;
    float2 uv            : TEXCOORD1;
};

float3 directionalContribution(float3 normal, float4 colorEnabled, float3 direction)
{
    if (colorEnabled.w <= 0.5f)
        return 0.0f;
    const float diffuse = saturate(dot(normal, -normalize(direction)));
    return colorEnabled.rgb * diffuse;
}

float3 spotlightContribution(float3 normal, float3 worldPosition)
{
    if (spotlightColorEnabled.w <= 0.5f)
        return 0.0f;

    const float3 toLight = spotlightPositionRange.xyz - worldPosition;
    const float distanceToLight = length(toLight);
    if (distanceToLight <= 0.000001f || distanceToLight > spotlightPositionRange.w)
        return 0.0f;

    const float3 lightDirection = toLight / distanceToLight;
    const float diffuse = saturate(dot(normal, lightDirection));
    if (diffuse <= 0.0f)
        return 0.0f;

    const float rho = dot(normalize(spotlightDirectionFalloff.xyz), -lightDirection);
    const float cosTheta = cos(spotlightAttenuationTheta.w * 0.5f);
    const float cosPhi = cos(spotlightPhi.x * 0.5f);
    float cone = 0.0f;
    if (rho >= cosTheta)
        cone = 1.0f;
    else if (rho > cosPhi)
    {
        const float coneRatio = saturate(
            (rho - cosPhi) / max(cosTheta - cosPhi, 0.000001f));
        cone = pow(coneRatio, max(spotlightDirectionFalloff.w, 0.0f));
    }

    const float3 attenuation = spotlightAttenuationTheta.xyz;
    const float denominator = attenuation.x +
        attenuation.y * distanceToLight +
        attenuation.z * distanceToLight * distanceToLight;
    const float distanceAttenuation = 1.0f / max(denominator, 0.000001f);
    return spotlightColorEnabled.rgb * diffuse * cone * distanceAttenuation;
}

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    const float4 localPosition = float4(input.position, 1.0f);
    output.position = mul(localPosition, worldViewProjection);
    const float3 worldPosition = mul(localPosition, world).xyz;
    const float3 normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    // Retail display.cpp forces the D3D Gouraud default: illuminate vertices,
    // then interpolate their lighting instead of renormalizing pixel normals.
    float3 lighting = sceneAmbient.rgb;
    lighting += directionalContribution(
        normal, boardReflectionColorEnabled, boardReflectionDirection.xyz);
    lighting += directionalContribution(normal, sunColorEnabled, sunDirection.xyz);
    lighting += spotlightContribution(normal, worldPosition);
    // Fixed-function diffuse color is material-modulated and clamped at
    // each vertex, before Gouraud interpolation and texture modulation.
    output.color = saturate(materialDiffuse.rgb * lighting);
    output.uv = input.uv;
    return output;
}
