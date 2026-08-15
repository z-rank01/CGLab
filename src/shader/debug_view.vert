#version 450
// R4/M3：调试视图全屏 quad 顶点着色器。仅消费 position（location 0），
// uv 由 NDC 位置推导并翻转 Y（Vulkan 纹理 v=0 在顶部，翻转后图像正立）。
layout(location = 0) in vec3 in_position;

layout(location = 0) out vec2 out_uv;

void main()
{
    out_uv = vec2(in_position.x, -in_position.y) * 0.5 + 0.5;
    gl_Position = vec4(in_position.xy, 0.0, 1.0);
}
