@group(0u) @binding(1u) var tex : texture_2d<f32>;

@group(0u) @binding(2u) var samp : sampler;

var<private> v : vec4<f32>;

fn fs_main_inner(v_1 : vec2<f32>) {
  v = textureSample(tex, samp, v_1);
}

@fragment
fn fs_main(@location(0u) v_2 : vec2<f32>) -> @location(0u) vec4<f32> {
  fs_main_inner(v_2);
  return v;
}
