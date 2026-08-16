#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 texcoord0;
layout(location = 3) in vec2 texcoord1;
layout(location = 4) flat in uint material_index;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform texture2D sampled_images[];
layout(set = 0, binding = 1) uniform sampler samplers[];

struct MaterialRow {
    vec4 base_color;
    vec4 emissive_metallic;
    vec4 roughness_alpha;
    vec4 texture_scales;
    uvec4 image_slots;
    uvec4 sampler_slots;
    uvec4 texcoords;
    uvec4 emissive_texture;
};

layout(set = 0, binding = 4, std430) readonly buffer MaterialTable {
    MaterialRow rows[];
} material_tables[];

// 光源表（复用 binding 4 storage buffer 表，按 lights_buffer_slot 索引；
// std430 三列连续；light_positions/light_colors 为 vec4 行）
layout(set = 0, binding = 4, std430) readonly buffer LightTable {
    vec4 light_positions[];
    vec4 light_colors[];
    float light_intensities[];
} light_tables[];

// 平行光 UBO（std140；intensity=0 表示无平行光，跳过阴影采样）
layout(set = 0, binding = 3) uniform LightUniform {
    mat4 view_proj;
    vec4 direction;
    vec4 color;
    float intensity;
    float pad0;
    float pad1;
    float pad2;
} light_uniforms[];

layout(push_constant) uniform ObjectPush {
    uint light_uniform_slot;
    uint transform_buffer_slot;
    uint frame_uniform_slot;
    uint material_buffer_slot;
    uint lights_buffer_slot;
    uint light_count;
    uint shadow_map_slot;
    uint shadow_sampler_slot;
} object_push;

const float PI = 3.14159265359;

vec2 material_uv(uint set_index)
{
    return set_index == 1 ? texcoord1 : texcoord0;
}

// 硬件 PCF 阴影（R2）：comparison sampler（LESS_OR_EQUAL）+ linear 滤波，
// 单次 dref 采样 = 硬件 2×2 比较平均；slope-scaled bias 进 reference 深度。
float sample_shadow(vec3 world_pos, vec3 normal, vec3 light_dir)
{
    uint light_slot = nonuniformEXT(object_push.light_uniform_slot);
    uint map_slot = nonuniformEXT(object_push.shadow_map_slot);
    uint smp_slot = nonuniformEXT(object_push.shadow_sampler_slot);
    vec4 light_proj = light_uniforms[light_slot].view_proj * vec4(world_pos, 1.0);
    vec3 ndc = light_proj.xyz / light_proj.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    // GLM_FORCE_DEPTH_ZERO_TO_ONE：NDC z 与 Vulkan 深度缓冲同刻度，直接比较
    float shadow_depth = ndc.z;
    float bias = max(0.001, 0.002 * (1.0 - max(dot(normal, light_dir), 0.0)));
    return texture(sampler2DShadow(sampled_images[map_slot], samplers[smp_slot]),
                   vec3(uv, shadow_depth - bias));
}

void main()
{
    uint table_slot = nonuniformEXT(object_push.material_buffer_slot);
    MaterialRow material = material_tables[table_slot].rows[material_index];
    uint base_image = nonuniformEXT(material.image_slots.x);
    uint base_sampler = nonuniformEXT(material.sampler_slots.x);
    vec4 base_color = material.base_color * texture(sampler2D(sampled_images[base_image], samplers[base_sampler]),
                                                     material_uv(material.texcoords.x));
    if (material.roughness_alpha.z == 1.0 && base_color.a < material.roughness_alpha.y) discard;

    uint normal_image = nonuniformEXT(material.image_slots.z);
    uint normal_sampler = nonuniformEXT(material.sampler_slots.z);
    vec3 tangent_normal = texture(sampler2D(sampled_images[normal_image], samplers[normal_sampler]),
                                  material_uv(material.texcoords.z)).xyz * 2.0 - 1.0;
    tangent_normal.xy *= material.texture_scales.x;
    vec3 dp1 = dFdx(world_position);
    vec3 dp2 = dFdy(world_position);
    vec2 duv1 = dFdx(material_uv(material.texcoords.z));
    vec2 duv2 = dFdy(material_uv(material.texcoords.z));
    vec3 base_normal = normalize(world_normal);
    vec3 tangent_raw = dp1 * duv2.y - dp2 * duv1.y;
    vec3 bitangent_raw = -dp1 * duv2.x + dp2 * duv1.x;
    vec3 normal = base_normal;
    if (dot(tangent_raw, tangent_raw) > 1e-10 && dot(bitangent_raw, bitangent_raw) > 1e-10)
        normal = normalize(mat3(normalize(tangent_raw), normalize(bitangent_raw), base_normal) * tangent_normal);

    // 平行光（主光）：方向/强度/颜色来自 light uniform；缺失时回退硬编码方向光。
    uint light_slot = nonuniformEXT(object_push.light_uniform_slot);
    vec3 sun_direction = normalize(-light_uniforms[light_slot].direction.xyz);
    float sun_intensity = light_uniforms[light_slot].intensity;
    vec3 sun_color = light_uniforms[light_slot].color.rgb;
    if (sun_intensity <= 0.0)
    {
        sun_direction = normalize(vec3(0.4, 1.0, 0.3));
        sun_intensity = 3.0;
        sun_color = vec3(1.0, 0.95, 0.9);
    }

    vec3 view_direction = normalize(-world_position);
    vec3 half_vector = normalize(view_direction + sun_direction);
    uint mr_image = nonuniformEXT(material.image_slots.y);
    uint mr_sampler = nonuniformEXT(material.sampler_slots.y);
    vec4 mr_sample = texture(sampler2D(sampled_images[mr_image], samplers[mr_sampler]), material_uv(material.texcoords.y));
    float metallic = clamp(material.emissive_metallic.w * mr_sample.b, 0.0, 1.0);
    float roughness = clamp(material.roughness_alpha.x * mr_sample.g, 0.04, 1.0);
    float ndotl = max(dot(normal, sun_direction), 0.0);
    float ndoth = max(dot(normal, half_vector), 0.0);
    vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(half_vector, view_direction), 0.0), 5.0);
    float shininess = mix(256.0, 2.0, roughness);
    vec3 specular = fresnel * pow(ndoth, shininess) * ndotl;
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color.rgb / PI;
    uint occlusion_image = nonuniformEXT(material.image_slots.w);
    uint occlusion_sampler = nonuniformEXT(material.sampler_slots.w);
    float sampled_occlusion = texture(sampler2D(sampled_images[occlusion_image], samplers[occlusion_sampler]),
                                      material_uv(material.texcoords.w)).r;
    float occlusion = mix(1.0, sampled_occlusion, material.texture_scales.y);
    uint emissive_image = nonuniformEXT(material.emissive_texture.x);
    uint emissive_sampler = nonuniformEXT(material.emissive_texture.y);
    vec3 emissive = material.emissive_metallic.xyz *
                    texture(sampler2D(sampled_images[emissive_image], samplers[emissive_sampler]),
                            material_uv(material.emissive_texture.z)).rgb;
    // 半球环境光：天空/地面双色按法线朝向混合，金属材质（diffuse≈0）靠它
    // 提供无 IBL 时的近似环境反射，避免整体发黑。
    vec3 sky_tint = vec3(0.55, 0.62, 0.72);
    vec3 ground_tint = vec3(0.30, 0.27, 0.24);
    vec3 ambient_color = mix(ground_tint, sky_tint, 0.5 + 0.5 * normal.y);
    vec3 ambient = base_color.rgb * ambient_color * 0.35 * occlusion;
    // 阴影只调制平行光贡献（点光保持无阴影，避免双重遮挡）
    float shadow = sample_shadow(world_position, normal, sun_direction);
    vec3 sun_light = (diffuse + specular) * ndotl * sun_intensity * shadow;
    vec3 point_light = vec3(0.0);
    if (object_push.light_count > 0)
    {
        // 点光补充：方向 = 灯 → 片元；强度来自通道
        uint point_slot = nonuniformEXT(object_push.lights_buffer_slot);
        vec3 point_dir = normalize(light_tables[point_slot].light_positions[0].xyz - world_position);
        float point_ndotl = max(dot(normal, point_dir), 0.0);
        point_light = light_tables[point_slot].light_colors[0].rgb *
                      light_tables[point_slot].light_intensities[0] * point_ndotl / PI;
    }
    vec3 color = ambient + sun_light * sun_color + point_light + emissive;
    out_color = vec4(color, base_color.a);
}
