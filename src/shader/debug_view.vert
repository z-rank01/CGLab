#version 450
// R4/M3：调试视图全屏 quad 顶点着色器。仅消费 position（location 0），
// uv 由 NDC 位置直接推导（v=0 ↔ NDC y=-1 ↔ 屏幕/纹理顶行，无需翻转）。
// 被采纹理（半分辨率 RT、阴影图）在生产侧已做 proj[1][1]*=-1，
// 纹理空间内图像正立；这里再翻 Y 会导致最终画面上下镜像（M6 回归）。
layout(location = 0) in vec3 in_position;

layout(location = 0) out vec2 out_uv;

void main()
{
    out_uv = in_position.xy * 0.5 + 0.5;
    gl_Position = vec4(in_position.xy, 0.0, 1.0);
}
