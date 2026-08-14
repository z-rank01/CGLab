#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inTexCoord0;
layout(location = 5) in vec2 inTexCoord1;

layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec2 texcoord0;
layout(location = 3) out vec2 texcoord1;
layout(location = 4) flat out uint material_index;

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

layout(push_constant) uniform ObjectPush {
    uint frame_uniform_slot;
    uint transform_buffer_slot;
    uint material_buffer_slot;
    uint lights_buffer_slot;
    uint light_count; // 与 frag 的 ObjectPush 布局一致（本 stage 未消费）
} object_push;

void main() 
{
    // proj * view * model（对象级 model 来自 push constant）
    uint frame_slot = nonuniformEXT(object_push.frame_uniform_slot);
    uint transform_slot = nonuniformEXT(object_push.transform_buffer_slot);
    TransformRow transform = transform_tables[transform_slot].rows[gl_InstanceIndex];
    mat4 model = transform.model;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = frame_uniforms[frame_slot].proj * frame_uniforms[frame_slot].view *
                  world;
    world_position = world.xyz;
    world_normal = normalize(mat3(transpose(inverse(model))) * inNormal);
    texcoord0 = inTexCoord0;
    texcoord1 = inTexCoord1;
    material_index = transform.metadata.x;
}
