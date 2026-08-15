#version 450
#extension GL_EXT_nonuniform_qualifier : require
// R4/M3：调试视图片段着色器——采样中间 RT（阴影图）并映射为可读颜色。
// mode 0：原始深度（近=白，远=黑）；mode 1：线性化距离热力图（近=蓝，远=红）。
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

void main()
{
    float depth = texture(sampler2D(sampled_images[debug_push.image_slot],
                                    samplers[debug_push.sampler_slot]), in_uv).r;
    vec3 color;
    if (debug_push.mode == 1u)
    {
        float linear = debug_push.near_plane + depth * (debug_push.far_plane - debug_push.near_plane);
        float t = clamp(linear / max(debug_push.far_plane, 0.0001), 0.0, 1.0);
        color = mix(vec3(0.2, 0.3, 1.0), vec3(1.0, 0.25, 0.1), t);
    }
    else
    {
        color = vec3(1.0 - depth);
    }
    // 高对比边框让“pass 没执行”和“纹理恰好全是远平面”可直接区分。
    // 约 2 px（320x180 inset）宽，不遮挡主体深度内容。
    vec2 edge = min(in_uv, vec2(1.0) - in_uv);
    if (min(edge.x, edge.y) < 0.01)
    {
        color = vec3(0.1, 0.9, 1.0);
    }
    out_color = vec4(color, 1.0);
}
