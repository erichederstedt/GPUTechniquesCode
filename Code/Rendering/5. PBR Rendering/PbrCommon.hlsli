#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

#ifndef PI
#define PI 3.14159265358979323846
#endif

float pow4(float x)
{
    return x*x*x*x;
}

float pow5(float x)
{
    return x*x*x*x*x;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / max(denom, 0.0001);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotL = max(dot(N,L),0.0);
    float NdotV = max(dot(N,V),0.0);
    return GeometrySchlickGGX(NdotL, roughness)*GeometrySchlickGGX(NdotV, roughness);
}
float3 fresnelSchlick(float3 F0, float3 V, float3 H)
{
    // TODO: To calculate Schlick F here
    float cosA = max(dot(V,H),0.0);
    float t = pow5(1.0 - cosA);
    return F0 + (float3(1.0, 1.0, 1.0)-F0) * t;
}

float3 Phong_BRDF(float3 N, float3 L, float3 V, float3 albedo, float shininess, float diffuse_power, float specular_power)
{
    float LdotN = saturate(dot(L, N));
    float3 R = reflect(-L, N);

    float3 diffuse = albedo * LdotN;
    float3 specular = pow(saturate(dot(V, R)), shininess);

    return diffuse_power * diffuse + specular_power * specular;
}

float3 Blinn_Phong_BRDF(float3 N, float3 L, float3 V, float3 albedo, float shininess, float diffuse_power, float specular_power)
{
    float LdotN = saturate(dot(L, N));
    float3 H = normalize(V + L);    

    float3 diffuse = albedo * LdotN;
    float3 specular = pow(saturate(dot(H, N)), shininess);

    return diffuse_power * diffuse + specular_power * specular;
}

float3 Cook_Torrance_BRDF(float3 N, float3 L, float3 V, float3 albedo, float shininess, float roughness)
{
    float LdotN = saturate(dot(L, N));
    float3 H = normalize(V + L);    

    float3 diffuse = albedo;
    float3 specular = pow(saturate(dot(H, N)), shininess);

    return LdotN * (roughness * diffuse + (1.0 - roughness) * specular);
}

float3 BRDF(float3 N, float3 L, float3 V, float3 albedo, float roughness, float metalness, Texture2D EoLUT, Texture2D EavgLUT, SamplerState smp)
{
    float LdotN = saturate(dot(L, N));

    #define BRDF_MODEL 2
    if (BRDF_MODEL == 0)
    {
        return Phong_BRDF(N, L, V, albedo, 40.0, 1.0, 1.0);
    }
    else if (BRDF_MODEL == 1)
    {
        return Blinn_Phong_BRDF(N, L, V, albedo, 40.0, 1.0, 1.0);
    }
    else if (BRDF_MODEL == 2)
    {
        return Cook_Torrance_BRDF(N, L, V, albedo, 40.0, 0.5);
    }

    return float3(1.0, 1.0, 1.0);
}

float attenuation(in float dist2, in float range2) // dist2 = dist * dist, range2 = range * range
{
#if false
// https://github.com/turanszkij/WickedEngine/blob/master/WickedEngine/shaders/lightingHF.hlsli#L145
    float dist_per_range = dist2 / max(0.0001, range2); // pow2
    dist_per_range *= dist_per_range; // pow4
    return saturate(1 - dist_per_range) / max(0.0001, dist2);
#else
// GLTF recommendation: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual#range-property
    return saturate(1 - pow(dist2 / range2, 4)) / dist2;
#endif
}

float angular_attenuation(in float3 light_dir, in float3 light_to_surface, in float outer_angle, in float inner_angle)
{
    float cos_angle = dot(light_dir, light_to_surface);
    float angle = acos(cos_angle);
    float penumbra = outer_angle - inner_angle;
    float falloff = saturate((angle - outer_angle) / penumbra);
    return saturate(1 - falloff);
}

float point_light_attenuation(in float intensity, in float distance, in float a, in float b, in float c)
{
    return intensity / max(0.0001, a + b*distance + c*distance*distance);
}

#endif