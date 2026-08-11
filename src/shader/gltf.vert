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

layout(set = 0, binding = 4, std430) readonly buffer TransformTable {
    mat4 models[];
} transform_tables[];

layout(push_constant) uniform ObjectPush {
    uint frame_uniform_slot;
    uint transform_buffer_slot;
} object_push;

void main() 
{
    // proj * view * model（对象级 model 来自 push constant）
    uint frame_slot = nonuniformEXT(object_push.frame_uniform_slot);
    uint transform_slot = nonuniformEXT(object_push.transform_buffer_slot);
    mat4 model = transform_tables[transform_slot].models[gl_InstanceIndex];
    gl_Position = frame_uniforms[frame_slot].proj * frame_uniforms[frame_slot].view *
                  model * vec4(inPosition, 1.0);
    
    // 使用顶点法线作为颜色，这样更容易看出几何形状正确性
    // 注意：法线需要归一化到 [0,1] 范围内显示
    fragColor = normalize(inNormal) * 0.5 + 0.5;
    
    // 或者直接使用输入颜色
    // fragColor = inColor.rgb;
}
