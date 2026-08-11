#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inTexCoord0;
layout(location = 5) in vec2 inTexCoord1;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 3) uniform MvpMatrix {
    mat4 model;
    mat4 view;
    mat4 proj;
} frame_uniforms[];

// P2：对象级变换走 push constant（per-draw 更新），
// uniform 中的 model 保留布局兼容（恒为 identity），view/proj 每帧更新。
layout(push_constant) uniform ObjectPush {
    mat4 model;
    uint frame_uniform_slot;
} object_push;

void main() 
{
    // proj * view * model（对象级 model 来自 push constant）
    uint frame_slot = nonuniformEXT(object_push.frame_uniform_slot);
    gl_Position = frame_uniforms[frame_slot].proj * frame_uniforms[frame_slot].view *
                  object_push.model * vec4(inPosition, 1.0);
    
    // 使用顶点法线作为颜色，这样更容易看出几何形状正确性
    // 注意：法线需要归一化到 [0,1] 范围内显示
    fragColor = normalize(inNormal) * 0.5 + 0.5;
    
    // 或者直接使用输入颜色
    // fragColor = inColor.rgb;
}
