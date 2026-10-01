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
    float4 mapFlags;
    float4 mapParameters;
    row_major float4x4 shadowViewProjection;
    float4 shadowParameters;
};
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D baseColorMap : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState baseColorSampler : register(s0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D metallicRoughnessMap : register(t1, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState metallicRoughnessSampler : register(s1, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D normalMap : register(t2, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState normalSampler : register(s2, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D emissiveMap : register(t3, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState emissiveSampler : register(s3, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D occlusionMap : register(t4, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState occlusionSampler : register(s4, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
TextureCube specularEnvironment : register(t5, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState specularEnvironmentSampler : register(s5, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D directionalShadowMap : register(t6, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState directionalShadowSampler : register(s6, space2);

struct PixelInput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
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
float3 directLight(float3 n, float3 v, float3 l, float3 radiance,
    float3 albedo, float metallic, float roughness)
{
    const float nDotL = saturate(dot(n, l));
    const float nDotV = saturate(dot(n, v));
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;
    const float3 h = safeNormalize(v + l);
    const float nDotH = saturate(dot(n, h));
    roughness = clamp(roughness, 0.045f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float nDotHSquared = nDotH * nDotH;
    const float divisor = (1.0f - nDotHSquared) + alphaSquared * nDotHSquared;
    const float distribution = alphaSquared / (PI * divisor * divisor);
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    const float3 fresnel = fresnelSchlick(dot(v, h), f0);
    const float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * albedo / PI;
    const float3 specular = distribution * visibilityGGX(nDotV, nDotL, alphaSquared) * fresnel;
    return (diffuse + specular) * radiance * nDotL;
}
float3 directionalLight(float3 n, float3 v, float4 colorEnabled, float3 direction,
    float3 albedo, float metallic, float roughness)
{
    return colorEnabled.w > 0.5f
        ? directLight(n, v, safeNormalize(-direction), colorEnabled.rgb, albedo, metallic, roughness) : 0.0f;
}
float3 spotlight(float3 n, float3 v, float3 position,
    float3 albedo, float metallic, float roughness)
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
    return directLight(n, v, l, spotlightColorEnabled.rgb * cone / max(denominator, 0.000001f),
        albedo, metallic, roughness);
}
float3 linearToSRGB(float3 color)
{
    color = saturate(color);
    return float3(
        color.r <= 0.0031308f ? color.r * 12.92f : 1.055f * pow(color.r, 1.0f / 2.4f) - 0.055f,
        color.g <= 0.0031308f ? color.g * 12.92f : 1.055f * pow(color.g, 1.0f / 2.4f) - 0.055f,
        color.b <= 0.0031308f ? color.b * 12.92f : 1.055f * pow(color.b, 1.0f / 2.4f) - 0.055f);
}
float3 mappedNormal(PixelInput input, float3 n)
{
    // Derivatives are evaluated before data-dependent branches.
    const float3 dpdx = ddx(input.worldPosition);
    const float3 dpdy = ddy(input.worldPosition);
    const float2 duvdx = ddx(input.uv);
    const float2 duvdy = ddy(input.uv);
    if (mapFlags.z <= 0.5f) return n;
    float3 tangent = input.tangent.xyz - n * dot(n, input.tangent.xyz);
    float3 bitangent;
    if (abs(input.tangent.w) > 0.5f && dot(tangent, tangent) > 0.00000001f)
    {
        tangent = safeNormalize(tangent);
        bitangent = cross(n, tangent) * input.tangent.w;
    }
    else
    {
        const float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
        if (abs(determinant) < 0.00000001f) return n;
        tangent = (dpdx * duvdy.y - dpdy * duvdx.y) / determinant;
        const float3 rawBitangent = (dpdy * duvdx.x - dpdx * duvdy.x) / determinant;
        tangent = tangent - n * dot(n, tangent);
        if (dot(tangent, tangent) < 0.00000001f) return n;
        tangent = safeNormalize(tangent);
        const float handedness = dot(cross(n, tangent), rawBitangent) < 0.0f ? -1.0f : 1.0f;
        bitangent = cross(n, tangent) * handedness;
    }
    float3 sampled = normalMap.Sample(normalSampler, input.uv).xyz * 2.0f - 1.0f;
    sampled.xy *= mapParameters.y;
    return safeNormalize(tangent * sampled.x + bitangent * sampled.y + n * sampled.z);
}
float3 environmentBRDF(float3 f0, float roughness, float noV)
{
    // Karis, "Physically Based Shading on Mobile" (Epic Games, 2014).
    // Analytic split-sum environment BRDF fit; no DFG lookup texture.
    const float4 r = saturate(roughness) * float4(-1.0f, -0.0275f, -0.572f, 0.022f)
        + float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float a004 = min(r.x * r.x, exp2(-9.28f * noV)) * r.x + r.y;
    const float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return max(f0 * ab.x + ab.y, 0.0f);
}
float directionalVisibility(float3 position, float3 n)
{
    if (shadowParameters.x <= 0.5f) return 1.0f;
    const float4 light = mul(float4(position, 1.0f), shadowViewProjection);
    const float3 projected = light.xyz / light.w;
    const float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
    // Receiver-plane depth correction: the orthographic map records depth at
    // texel centers. A sloping receiver must be compared at those same points,
    // not at the current pixel's center depth for all nine PCF neighbors.
    const float2 dx = ddx(uv);
    const float2 dy = ddy(uv);
    const float dzdx = ddx(projected.z);
    const float dzdy = ddy(projected.z);
    const float determinant = dx.x * dy.y - dx.y * dy.x;
    float2 depthGradient = 0.0f;
    if (abs(determinant) > 0.000000000001f)
        depthGradient = clamp(float2(dzdx * dy.y - dzdy * dx.y,
            dx.x * dzdy - dy.x * dzdx) / determinant, -8.0f, 8.0f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || projected.z < 0.0f || projected.z > 1.0f) return 1.0f;
    const float slope = 1.0f - saturate(dot(n, safeNormalize(-sunDirection.xyz)));
    const float bias = shadowParameters.z * (1.0f + 2.0f * slope);
    float visible = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUV = uv + float2(x, y) * shadowParameters.y;
            const float2 texelCenter = clamp((floor(sampleUV / shadowParameters.y) + 0.5f)
                * shadowParameters.y, shadowParameters.y * 0.5f, 1.0f - shadowParameters.y * 0.5f);
            const float depth = directionalShadowMap.SampleLevel(directionalShadowSampler, texelCenter, 0).r;
            const float receiverDepth = projected.z + dot(depthGradient, texelCenter - uv);
            visible += receiverDepth - bias <= depth ? 1.0f : 0.0f;
        }
    }
    return visible / 9.0f;
}
float4 main(PixelInput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
    float4 sampledBaseColor = baseColor;
    if (mapFlags.x > 0.5f) sampledBaseColor *= baseColorMap.Sample(baseColorSampler, input.uv);
    if (metallicRoughness.w > 0.5f && sampledBaseColor.a < mapParameters.w) discard;
    // The same real mesh/alpha-mask shader writes light-space depth into an
    // R32_FLOAT target, with a hardware depth attachment choosing the nearest.
    if (shadowParameters.x < -0.5f) return float4(input.position.z, 0.0f, 0.0f, 1.0f);
    const float3 albedo = sampledBaseColor.rgb;
    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y;
    if (mapFlags.y > 0.5f)
    {
        const float4 texel = metallicRoughnessMap.Sample(metallicRoughnessSampler, input.uv);
        roughness *= texel.g;
        metallic *= texel.b;
    }
    metallic = saturate(metallic);
    const float3 n = mappedNormal(input, safeNormalize(input.normal)) * (frontFace ? 1.0f : -1.0f);
    const float3 v = safeNormalize(cameraPosition.xyz - input.worldPosition);
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    const float3 fresnel = fresnelSchlick(saturate(dot(n, v)), f0);
    float3 color;
    if (sceneAmbient.w > 0.5f)
    {
        // Mips contain GGX-prefiltered linear HDR radiance at perceptual
        // roughness mip/(levelCount-1). The current diffuse approximation stays.
        uint width, height, levelCount;
        specularEnvironment.GetDimensions(0, width, height, levelCount);
        const float lod = saturate(roughness) * float(max(levelCount, 1u) - 1u);
        const float3 radiance = specularEnvironment.SampleLevel(specularEnvironmentSampler,
            reflect(-v, n), lod).rgb;
        const float3 specular = radiance * environmentBRDF(f0, roughness, saturate(dot(n, v)));
        color = sceneAmbient.rgb * ((1.0f - fresnel) * (1.0f - metallic) * albedo + specular);
    }
    else
    {
        // Preserve the pre-environment factor-ambient expression exactly.
        color = sceneAmbient.rgb * ((1.0f - fresnel) * (1.0f - metallic) * albedo + f0);
    }
    if (mapParameters.x > 0.5f)
    {
        const float occlusion = occlusionMap.Sample(occlusionSampler, input.uv).r;
        color *= lerp(1.0f, occlusion, saturate(mapParameters.z));
    }
    color += directionalLight(n, v, boardReflectionColorEnabled, boardReflectionDirection.xyz,
        albedo, metallic, roughness);
    color += directionalLight(n, v, sunColorEnabled, sunDirection.xyz, albedo, metallic, roughness)
        * directionalVisibility(input.worldPosition, n);
    color += spotlight(n, v, input.worldPosition, albedo, metallic, roughness);
    float3 emissive = emissiveStrength.rgb * max(emissiveStrength.w, 0.0f);
    if (mapFlags.w > 0.5f) emissive *= emissiveMap.Sample(emissiveSampler, input.uv).rgb;
    color += emissive;
    if (shadowParameters.w > 0.5f)
        color = saturate((color * (2.51f * color + 0.03f)) /
            (color * (2.43f * color + 0.59f) + 0.14f));
    return float4(metallicRoughness.z > 0.5f ? color : linearToSRGB(color), 1.0f);
}
