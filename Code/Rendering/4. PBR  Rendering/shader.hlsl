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

cbuffer model_cbuffer : register(b0)
{
    float4x4 model_to_world;
}

cbuffer camera_cbuffer : register(b1)
{
    float4x4 world_to_clip;
}

Texture2D color_texture : register(t0);

[RootSignature("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), CBV(b0), CBV(b1), DescriptorTable(SRV(t0)), StaticSampler(s0)")]
vs_out VSMain(vs_in In)
{
    vs_out Out;

    Out.ws_pos = mul(model_to_world, float4(In.pos, 1.0));
    Out.cs_pos = mul(world_to_clip, Out.ws_pos);
    Out.ws_normal = normalize(mul(model_to_world, float4(In.normal, 0.0)));
    Out.ws_tangent = normalize(mul(model_to_world, float4(In.tangent, 0.0)));
    Out.ws_bitangent = normalize(mul(model_to_world, float4(In.bitangent, 0.0)));
    Out.color = In.color;
    In.uv.y = In.uv.y;
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
    float4 color = color_texture.Sample(Sampler, In.uv);

    // if (color.a < 0.5f)
        // discard;

    color.rgb = (In.ws_normal.rgb);
    color.a = 1.0;
    return color;
}