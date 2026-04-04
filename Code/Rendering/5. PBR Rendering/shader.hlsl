struct vs_in
{
    float3 pos : POS;
    float4 color : COL;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
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

struct Light_Info
{
    uint type;
    float4 color;
    float3 pos;
    float radius;
    float3 dir;
    float inner_cone_angle;
    float outer_cone_angle;
};
StructuredBuffer<Light_Info> light_buffer : register(t1);

cbuffer model_cbuffer : register(b0)
{
    float4x4 model_to_world;
}

cbuffer camera_cbuffer : register(b1)
{
    float4x4 world_to_clip;
}

Texture2D color_texture : register(t0);

[RootSignature("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), CBV(b0), CBV(b1), DescriptorTable(SRV(t0)), DescriptorTable(SRV(t1)), StaticSampler(s0)")]
vs_out VSMain(vs_in In)
{
    vs_out Out;

    Out.ws_pos = mul(model_to_world, float4(In.pos, 1.0));
    Out.cs_pos = mul(world_to_clip, Out.ws_pos);
    Out.ws_normal = normalize(mul(model_to_world, float4(In.normal, 0.0)));
    Out.ws_tangent = normalize(mul(model_to_world, float4(In.tangent, 0.0)));
    Out.ws_bitangent = normalize(mul(model_to_world, float4(In.bitangent, 0.0)));
    Out.color = In.color;
    In.uv.y = 1.0 - In.uv.y;
    Out.uv = In.uv;
    
    return Out;
}

SamplerState Sampler : register(s0)
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};
float4 PSMain(vs_out In) : SV_TARGET
{
    return color_texture.Sample(Sampler, In.uv);
}