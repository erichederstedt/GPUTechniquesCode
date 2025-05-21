struct vs_in
{
    float3 pos : POS;
    float4 color : COL;
    float3 normal : NORMAL;
    float2 uv : UV;
};

struct vs_out
{
    float4 ws_pos : WS_POSITION;
    float4 cs_pos : SV_POSITION;
    float4 color : COL;
};

cbuffer model_cbuffer : register(b0)
{
    float4x4 model_to_world;
}

cbuffer camera_cbuffer : register(b1)
{
    float4x4 world_to_clip;
}

[RootSignature("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), CBV(b0), CBV(b1)")]
vs_out VSMain(vs_in In)
{
    vs_out Out;
    
    Out.ws_pos = mul(model_to_world, float4(In.pos, 1.0));
    Out.cs_pos = mul(world_to_clip, Out.ws_pos);
    Out.color = In.color;
    
    return Out;
}

float4 PSMain(vs_out In) : SV_TARGET
{
    return float4(sqrt(In.color.rgb), 1.0);
}