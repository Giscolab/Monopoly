cbuffer MaterialUniforms : register(b0, space3)
{
    float4 baseColor;
    float4 metallicRoughness;
    float4 emissiveStrength;
    float4 cameraPosition;
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
struct PixelInput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
};
static const float PI = 3.14159265358979323846f;

float3 safeNormalize(float3 value)
{
    return value * rsqrt(max(dot(value, value), 0.00000001f));
}
float3 fresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosine), 5.0f);
}
float visibilityGGX(float nDotV, float nDotL, float alphaSquared)
{
    const float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0f - alphaSquared) + alphaSquared);
    const float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0f - alphaSquared) + alphaSquared);
    return 0.5f / max(ggxV + ggxL, 0.000001f);
}
float3 directLight(float3 n, float3 v, float3 l, float3 radiance)
{
    const float nDotL = saturate(dot(n, l));
    const float nDotV = saturate(dot(n, v));
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;
    const float3 h = safeNormalize(v + l);
    const float nDotH = saturate(dot(n, h));
    const float metallic = saturate(metallicRoughness.x);
    const float roughness = clamp(metallicRoughness.y, 0.045f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float nDotHSquared = nDotH * nDotH;
    const float divisor = (1.0f - nDotHSquared) + alphaSquared * nDotHSquared;
    const float distribution = alphaSquared / (PI * divisor * divisor);
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor.rgb, metallic);
    const float3 fresnel = fresnelSchlick(dot(v, h), f0);
    const float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor.rgb / PI;
    const float3 specular = distribution * visibilityGGX(nDotV, nDotL, alphaSquared) * fresnel;
    return (diffuse + specular) * radiance * nDotL;
}
float3 directionalLight(float3 n, float3 v, float4 colorEnabled, float3 direction)
{
    return colorEnabled.w > 0.5f
        ? directLight(n, v, safeNormalize(-direction), colorEnabled.rgb) : 0.0f;
}
float3 spotlight(float3 n, float3 v, float3 position)
{
    if (spotlightColorEnabled.w <= 0.5f) return 0.0f;
    const float3 delta = spotlightPositionRange.xyz - position;
    const float distanceToLight = length(delta);
    if (distanceToLight <= 0.000001f || distanceToLight > spotlightPositionRange.w) return 0.0f;
    const float3 l = delta / distanceToLight;
    const float rho = dot(safeNormalize(spotlightDirectionFalloff.xyz), -l);
    const float cosTheta = cos(spotlightAttenuationTheta.w * 0.5f);
    const float cosPhi = cos(spotlightPhi.x * 0.5f);
    float cone = rho >= cosTheta ? 1.0f : 0.0f;
    if (rho < cosTheta && rho > cosPhi)
        cone = pow(saturate((rho - cosPhi) / max(cosTheta - cosPhi, 0.000001f)),
            max(spotlightDirectionFalloff.w, 0.0f));
    const float3 attenuation = spotlightAttenuationTheta.xyz;
    const float denominator = attenuation.x + attenuation.y * distanceToLight
        + attenuation.z * distanceToLight * distanceToLight;
    return directLight(n, v, l, spotlightColorEnabled.rgb * cone / max(denominator, 0.000001f));
}
float3 linearToSRGB(float3 color)
{
    color = saturate(color);
    return float3(
        color.r <= 0.0031308f ? color.r * 12.92f : 1.055f * pow(color.r, 1.0f / 2.4f) - 0.055f,
        color.g <= 0.0031308f ? color.g * 12.92f : 1.055f * pow(color.g, 1.0f / 2.4f) - 0.055f,
        color.b <= 0.0031308f ? color.b * 12.92f : 1.055f * pow(color.b, 1.0f / 2.4f) - 0.055f);
}
float4 main(PixelInput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
    const float3 n = safeNormalize(frontFace ? input.normal : -input.normal);
    const float3 v = safeNormalize(cameraPosition.xyz - input.worldPosition);
    const float metallic = saturate(metallicRoughness.x);
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor.rgb, metallic);
    const float3 fresnel = fresnelSchlick(saturate(dot(n, v)), f0);
    // Factor-only ambient approximation; no environment map is available.
    float3 color = sceneAmbient.rgb * ((1.0f - fresnel) * (1.0f - metallic) * baseColor.rgb + f0);
    color += directionalLight(n, v, boardReflectionColorEnabled, boardReflectionDirection.xyz);
    color += directionalLight(n, v, sunColorEnabled, sunDirection.xyz);
    color += spotlight(n, v, input.worldPosition);
    color += emissiveStrength.rgb * max(emissiveStrength.w, 0.0f);
    return float4(metallicRoughness.z > 0.5f ? color : linearToSRGB(color), baseColor.a);
}
