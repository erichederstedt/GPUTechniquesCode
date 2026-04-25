struct vs_in
{
    float3 pos : POS;
    float4 color : COL;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : UV;
};

struct vs_out
{
    float4 ws_pos : WS_POSITION;
    float4 cs_pos : SV_POSITION;
    float4 ws_normal : WS_NORMAL;
    float4 ws_tangent : WS_TANGENT;
    float4 ws_bitangent : WS_BITANGENT;
    float4 color : COL;
    float2 uv : UV;
};

#define LIGHT_TYPE_POINT 0
#define LIGHT_TYPE_SPOT 1
#define LIGHT_TYPE_DIRECTIONAL 2
struct Light_Info
{
    uint type;
    float pad0;
    float pad1;
    float pad2;
    float4 color;
    float3 pos;
    float radius;
    float3 dir;
    float inner_cone_angle;
    float outer_cone_angle;
    float pad3;
    float pad4;
    float pad5;
};
StructuredBuffer<Light_Info> light_buffer : register(t0);

cbuffer model_cbuffer : register(b0)
{
    float4x4 model_to_world;
}

cbuffer main_cbuffer : register(b1)
{
    float4x4 world_to_clip;
    float3 camera_position;
    uint lights;
}

Texture2D eavg_lut : register(t1);
Texture2D eo_lut : register(t2);
Texture2D color_texture : register(t3);
Texture2D normal_texture : register(t4);

[RootSignature("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), CBV(b0), CBV(b1), DescriptorTable(SRV(t0)), DescriptorTable(SRV(t1)), DescriptorTable(SRV(t2)), DescriptorTable(SRV(t3)), DescriptorTable(SRV(t4)), StaticSampler(s0)")]
vs_out VSMain(vs_in In)
{
    vs_out Out;

    Out.ws_pos = mul(model_to_world, float4(In.pos, 1.0));
    Out.cs_pos = mul(world_to_clip, Out.ws_pos);
    Out.ws_normal = normalize(mul(model_to_world, float4(In.normal, 0.0)));
    Out.ws_tangent = normalize(mul(model_to_world, float4(In.tangent.xyz, 0.0)));
    Out.ws_bitangent = normalize(mul(model_to_world, float4(cross(In.normal, In.tangent.xyz) * In.tangent.w, 0.0))); // cross(normal, tangent) * sign
    Out.color = In.color;
    In.uv.y = 1.0 - In.uv.y;
    Out.uv = In.uv;
    
    return Out;
}

#include "PbrCommon.hlsli"

SamplerState Sampler : register(s0)
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};
float4 PSMain(vs_out In) : SV_TARGET
{
    float4 color = color_texture.Sample(Sampler, In.uv);
    float3 normal = normal_texture.Sample(Sampler, In.uv).rgb * 2.0 - 1.0;

    float3x3 TBN = float3x3(
		normalize(In.ws_tangent.xyz),
		normalize(In.ws_bitangent.xyz),
		normalize(In.ws_normal.xyz)
	);
    TBN = transpose(TBN);
	float3 pixel_normal = -normalize(mul(TBN, normal));

    float3 albedo = color.rgb;
    float metallic = 0.0f;
    float roughness = 0.5f;
    
    float3 light = float3(0, 0, 0);
    for (unsigned int i = 0; i < lights; i++) 
    {
        Light_Info light_info = light_buffer[i];
        if (light_info.type == LIGHT_TYPE_DIRECTIONAL) 
        {
            float3 L = -light_info.dir;
            float3 V = normalize(camera_position - In.ws_pos.xyz);
            float3 N = pixel_normal;

            light += BRDF(L, N, V, albedo, roughness, metallic, eo_lut, eavg_lut, Sampler) * light_info.color.rgb * light_info.color.a;
        }
    }

    return float4(light, color.a);
    //return float4(pixel_normal, color.a);
}