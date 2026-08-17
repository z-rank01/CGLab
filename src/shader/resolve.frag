#version 450
#extension GL_EXT_nonuniform_qualifier : require
// M6/R5：resolve 片元着色器——采样半分辨率中间 RT，ACES 曲线整形 +
// 轻微 vignette 后写 swapchain。全屏 NDC quad（vertex 复用 debug_view.vert）。
// 注意：中间 RT 当前是 R8G8B8A8_UNORM（RG format 枚举尚无浮点格式），输入已被
// 钳到 [0,1]，ACES 在此做 LDR 曲线重映射而非真 HDR 压缩；真 HDR 链路需先在 RG
// 增加浮点 color 格式（如 R16G16B16A16_FLOAT），届时本 shader 无需改动。
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
    vec3 scene_color = texture(sampler2D(sampled_images[image_slot], samplers[smp_slot]), in_uv).rgb *
                       resolve_push.exposure;
    vec3 color = aces_tonemap(scene_color);
    // 轻微 vignette：风格化边缘压暗（审美取舍，与半分辨率上采样无关）
    vec2 centered = in_uv - 0.5;
    color *= 1.0 - 0.15 * dot(centered, centered) * 4.0;
    out_color = vec4(color, 1.0);
}
