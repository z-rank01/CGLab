#version 450
#extension GL_EXT_nonuniform_qualifier : require
// M6/R5：resolve 片元着色器——采样半分辨率 HDR 中间 RT，ACES 色调映射 +
// 轻微 vignette 后写 swapchain。全屏 NDC quad（vertex 复用 debug_view.vert）。
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform texture2D sampled_images[];
layout(set = 0, binding = 1) uniform sampler samplers[];

layout(push_constant) uniform ResolvePush {
    uint image_slot;
    uint sampler_slot;
    float exposure;
} resolve_push;

// ACES filmic tonemap（Narkowicz 2015 近似，无查找表）
vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    uint image_slot = nonuniformEXT(resolve_push.image_slot);
    uint smp_slot = nonuniformEXT(resolve_push.sampler_slot);
    vec3 hdr = texture(sampler2D(sampled_images[image_slot], samplers[smp_slot]), in_uv).rgb *
               resolve_push.exposure;
    vec3 color = aces_tonemap(hdr);
    // 轻微 vignette：边缘压暗，缓解半分辨率上采样的边缘感
    vec2 centered = in_uv - 0.5;
    color *= 1.0 - 0.15 * dot(centered, centered) * 4.0;
    out_color = vec4(color, 1.0);
}
