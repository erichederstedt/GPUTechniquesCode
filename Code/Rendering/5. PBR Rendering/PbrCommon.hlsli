#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

// #define ENABLE_ANISOTROPIC
// #define ENABLE_CLEARCOAT
// #define ENABLE_SHEEN
// #define ENABLE_SUBSURFACE

#ifndef PI
#define PI 3.14159265358979323846
#endif

struct Material_Properties
{
    float3 baseColor;
    float metallic;
    float roughness;

    float specular;
    float specularTint;

#ifdef ENABLE_SUBSURFACE
    float subsurface;
#endif
#ifdef ENABLE_SHEEN
    float sheen;
    float sheenTint;
#endif
#ifdef ENABLE_CLEARCOAT
    float clearcoat;
    float clearcoatGloss;
#endif
};
/*
static const float3 baseColor = float3(0.5, 0.5, 0.5); ,
static const float metallic = 0; ,
static const float subsurface = 0; ,
static const float specular = 0.5; ,
static const float roughness = 0.5; ,
static const float specularTint = 0; ,
static const float sheen = 0; ,
static const float sheenTint = 0.5;
static const float clearcoat = 0;
static const float clearcoatGloss = 1;
*/

float sqr(float x)
{
    return x * x;
}

float SchlickFresnel(float u)
{
    float m = clamp(1 - u, 0, 1);
    float m2 = m * m;
    return m2 * m2 * m; // pow(m,5)
}

float GTR1(float NdotH, float a)
{
    if (a >= 1)
        return 1 / PI;
    float a2 = a * a;
    float t = 1 + (a2 - 1) * NdotH * NdotH;
    return (a2 - 1) / (PI * log(a2) * t);
}

float GTR2(float NdotH, float a)
{
    float a2 = a * a;
    float t = 1 + (a2 - 1) * NdotH * NdotH;
    return a2 / (PI * t * t);
}

float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
    return 1 / (PI * ax * ay * sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
}

float smithG_GGX(float NdotV, float alphaG)
{
    float a = alphaG * alphaG;
    float b = NdotV * NdotV;
    return 1 / (NdotV + sqrt(a + b - a * b));
}

float smithG_GGX_aniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
{
    return 1 / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
}

float3 mon2lin(float3 x)
{
    return float3(pow(x[0], 2.2), pow(x[1], 2.2), pow(x[2], 2.2));
}

// L: light vector(normalized), V: view vector(normalized), N: normal(normalized), X: tangent(normalized), Y: bitangent(normalized)
#ifdef ENABLE_ANISOTROPIC
float3 BRDF(float3 L, float3 V, float3 N, float3 X, float3 Y, Material_Properties mat_properties)
#else
float3 BRDF(in float3 L, in float3 V, in float3 N, in Material_Properties mat_properties)
#endif
{
    float3 baseColor = mat_properties.baseColor;
    float metallic = mat_properties.metallic;
    float roughness = mat_properties.roughness;

    float specular = 0.5;
    float specularTint = 0;

#ifdef ENABLE_SUBSURFACE
    float subsurface = mat_properties.subsurface;
#else
    float subsurface = 0;
#endif
#ifdef ENABLE_ANISOTROPIC
    float anisotropic = 0;
#endif
#ifdef ENABLE_SHEEN
    float sheen = 0;
    float sheenTint = 0.5;
#endif
#ifdef ENABLE_CLEARCOAT
    float clearcoat = 0;
    float clearcoatGloss = 1;
#endif
    /* NOTE(ERIC): PROBLEM BIG
    There is a big issue with the NdotV currently. For some reason pixels within view that should not get negative end up sometimes at certain angles, returning negative values. 
    This is mega fucked and should not happen. Why this happens is a mystery, my first guess would be smoothed normals somehow causing something to be incorrect but I have zero evidence for that.
    */

    float NdotL = dot(N, L);
    float NdotV = saturate(dot(N, V));
    if (NdotL < 0 || NdotV < 0)
        return float3(0, 0, 0);

    float3 H = normalize(L + V);
    float NdotH = dot(N, H);
    float LdotH = dot(L, H);

    //float3 Cdlin = mon2lin(baseColor);
    float3 Cdlin = baseColor;
    float Cdlum = 0.3 * Cdlin[0] + 0.6 * Cdlin[1] + 0.1 * Cdlin[2]; // luminance approx.

    float3 Ctint = Cdlum > 0 ? Cdlin / Cdlum : float3(1, 1, 1); // normalize lum. to isolate hue+sat
    float3 Cspec0 = lerp(specular * 0.08 * lerp(float3(1, 1, 1), Ctint, specularTint), Cdlin, metallic);
#ifdef ENABLE_SHEEN
    float3 Csheen = lerp(float3(1, 1, 1), Ctint, sheenTint);
#endif

// Diffuse fresnel - go from 1 at normal incidence to .5 at grazing
// and mix in diffuse retro-reflection based on roughness
    float FL = SchlickFresnel(NdotL), FV = SchlickFresnel(NdotV);
    float Fd90 = 0.5 + 2 * LdotH * LdotH * roughness;
    float Fd = lerp(1.0, Fd90, FL) * lerp(1.0, Fd90, FV);

// Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
// 1.25 scale is used to (roughly) preserve albedo
// Fss90 used to "flatten" retroreflection based on roughness
    float Fss90 = LdotH * LdotH * roughness;
    float Fss = lerp(1.0, Fss90, FL) * lerp(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1 / (NdotL + NdotV) - .5) + .5);

// specular
    float FH = SchlickFresnel(LdotH);
    float3 Fs = lerp(Cspec0, float3(1.0, 1.0, 1.0), FH);
#ifdef ENABLE_ANISOTROPIC
    float aspect = sqrt(1 - anisotropic * .9);
    float ax = max(.001, sqr(roughness) / aspect);
    float ay = max(.001, sqr(roughness) * aspect);
    float Ds = GTR2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay);
    float Gs;
    Gs = smithG_GGX_aniso(NdotL, dot(L, X), dot(L, Y), ax, ay);
    Gs *= smithG_GGX_aniso(NdotV, dot(V, X), dot(V, Y), ax, ay);
#else
    float a = sqr(roughness);
    float Ds = GTR2(NdotH, a);
    float Gs = smithG_GGX(NdotL, a) * smithG_GGX(NdotV, a);
#endif

// sheen
#ifdef ENABLE_SHEEN
    float3 Fsheen = FH * sheen * Csheen;
#else
    float3 Fsheen = float3(0, 0, 0);
#endif

// clearcoat (ior = 1.5 -> F0 = 0.04)
#ifdef ENABLE_CLEARCOAT
    float Dr = GTR1(NdotH, lerp(.1, .001, clearcoatGloss));
    float Fr = lerp(.04, 1.0, FH);
    float Gr = smithG_GGX(NdotL, .25) * smithG_GGX(NdotV, .25);
#endif

    float3 diff = ( lerp(Fd, ss, subsurface) * Cdlin + Fsheen) * (1 - metallic) * max(0, NdotL);
    float3 spec = Gs * Fs * Ds;
#ifdef ENABLE_CLEARCOAT
    float3 clear = .25 * clearcoat * Gr * Fr * Dr;
#else
    float3 clear = float3(0, 0, 0);
#endif

    return diff + spec + clear;
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

#endif