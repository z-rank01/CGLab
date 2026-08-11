#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 3) uniform MvpMatrix {
    mat4 model;
    mat4 view;
    mat4 proj;
} frame_uniforms[];

struct TransformRow {
    mat4 model;
    uvec4 metadata;
};

layout(set = 0, binding = 4, std430) readonly buffer TransformTable {
    TransformRow rows[];
} transform_tables[];

layout(push_constant) uniform TrianglePush {
    uint frame_slot;
    uint transform_slot;
} triangle_push;

void main() 
{
    uint frame_slot = nonuniformEXT(triangle_push.frame_slot);
    TransformRow transform = transform_tables[nonuniformEXT(triangle_push.transform_slot)].rows[gl_InstanceIndex];
    gl_Position = frame_uniforms[frame_slot].proj * frame_uniforms[frame_slot].view *
                  transform.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}
