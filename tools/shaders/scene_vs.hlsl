struct Uniforms
{
    float4x4 mvp;
};
[[vk::binding(0, 0)]] ConstantBuffer<Uniforms> uniforms : register(b0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut vs_main(float3 inPos : POSITION, float2 inUV : TEXCOORD0)
{
    VSOut o;
    o.pos = mul(uniforms.mvp, float4(inPos, 1.0));
    o.uv = inUV;
    return o;
}
