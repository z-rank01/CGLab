#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;

// 平行光 UBO（std140；与 gltf.frag 的 LightUniform 块一致，仅消费 view_proj）
layout(set = 0, binding = 3) uniform LightUniform {
    mat4 view_proj;
    vec4 direction;
    vec4 color;
    float intensity;
    float pad0;
    float pad1;
    float pad2;
} light_uniforms[];

struct TransformRow {
    mat4 model;
    uvec4 metadata;
};

layout(set = 0, binding = 4, std430) readonly buffer TransformTable {
    TransformRow rows[];
} transform_tables[];

// shadow pass 只推前 8 字节（light_uniform_slot + transform_buffer_slot）
layout(push_constant) uniform ShadowPush {
    uint light_uniform_slot;
    uint transform_buffer_slot;
} shadow_push;

void main()
{
    uint light_slot = nonuniformEXT(shadow_push.light_uniform_slot);
    uint transform_slot = nonuniformEXT(shadow_push.transform_buffer_slot);
    TransformRow transform = transform_tables[transform_slot].rows[gl_InstanceIndex];
    gl_Position = light_uniforms[light_slot].view_proj * transform.model * vec4(inPosition, 1.0);
}
