cbuffer MaterialUniforms : register(b0, space3)
{
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

Texture2D legacyTexture : register(t0, space2);
SamplerState legacySampler : register(s0, space2);

struct PixelInput
{
    float4 position      : SV_Position;
    float3 normal        : TEXCOORD0;
    float2 uv            : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};

float3 directionalContribution(float3 normal, float4 colorEnabled, float3 direction)
{
    if (colorEnabled.w <= 0.5f)
        return 0.0f;
    const float diffuse = saturate(dot(normal, -normalize(direction)));
    return colorEnabled.rgb * diffuse;
}

float3 spotlightContribution(PixelInput input)
{
    if (spotlightColorEnabled.w <= 0.5f)
        return 0.0f;

    const float3 toLight = spotlightPositionRange.xyz - input.worldPosition;
    const float distanceToLight = length(toLight);
    if (distanceToLight <= 0.000001f || distanceToLight > spotlightPositionRange.w)
        return 0.0f;

    const float3 lightDirection = toLight / distanceToLight;
    const float diffuse = saturate(dot(normalize(input.normal), lightDirection));
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

float4 main(PixelInput input) : SV_Target0
{
    const float4 texel = legacyTexture.Sample(legacySampler, input.uv);
    const float3 normal = normalize(input.normal);
    float3 light = sceneAmbient.rgb;
    light += directionalContribution(
        normal, boardReflectionColorEnabled, boardReflectionDirection.xyz);
    light += directionalContribution(normal, sunColorEnabled, sunDirection.xyz);
    light += spotlightContribution(input);
    return float4(
        texel.rgb * materialDiffuse.rgb * light,
        texel.a * materialDiffuse.a);
}
