[[vk::binding(1, 0)]] Texture2D tex : register(t0);
[[vk::binding(2, 0)]] SamplerState samp : register(s0);

float4 fs_main(float2 uv : TEXCOORD0) : SV_Target
{
    return tex.Sample(samp, uv);
}
