#version 450
#extension GL_EXT_nonuniform_qualifier : require
// R4/M3：调试视图片段着色器——采样中间 RT（阴影图/半分辨率 RT）并映射为可读颜色。
// mode 语义（M6/R5 扩）：0=原始深度（近=白，远=黑）；1=线性化距离热力图（近=蓝，远=红）；
// 2=半分辨率 RT 原始（hdr）；3=半分辨率 RT + ACES tonemap（resolved）。
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform texture2D sampled_images[];
layout(set = 0, binding = 1) uniform sampler samplers[];

layout(push_constant) uniform DebugPush {
    uint image_slot;
    uint sampler_slot;
    uint mode;
    float near_plane;
    float far_plane;
} debug_push;

// ACES filmic tonemap（与 resolve.frag 同款，着色器无 include 机制故重复）
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
    uint image_slot = nonuniformEXT(debug_push.image_slot);
    uint smp_slot = nonuniformEXT(debug_push.sampler_slot);
    vec4 sample_value = texture(sampler2D(sampled_images[image_slot], samplers[smp_slot]), in_uv);
    vec3 color;
    if (debug_push.mode == 1u)
    {
        float linear = debug_push.near_plane + sample_value.r * (debug_push.far_plane - debug_push.near_plane);
        float t = clamp(linear / max(debug_push.far_plane, 0.0001), 0.0, 1.0);
        color = mix(vec3(0.2, 0.3, 1.0), vec3(1.0, 0.25, 0.1), t);
    }
    else if (debug_push.mode == 2u)
    {
        // hdr：半分辨率 RT 原始（线性，可能 >1 被显示端钳制）
        color = sample_value.rgb;
    }
    else if (debug_push.mode == 3u)
    {
        // resolved：与 resolve pass 同款 ACES tonemap
        color = aces_tonemap(sample_value.rgb);
    }
    else
    {
        color = vec3(1.0 - sample_value.r);
    }
    // 高对比边框让"pass 没执行"和"纹理恰好全是远平面"可直接区分。
    // 约 2 px（320x180 inset）宽，不遮挡主体深度内容。
    vec2 edge = min(in_uv, vec2(1.0) - in_uv);
    if (min(edge.x, edge.y) < 0.01)
    {
        color = vec3(0.1, 0.9, 1.0);
    }
    out_color = vec4(color, 1.0);
}
