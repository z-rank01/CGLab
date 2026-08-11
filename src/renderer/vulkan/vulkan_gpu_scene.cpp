#include "renderer/vulkan/vulkan_backend_internal.h"

#include <span>

namespace
{
    constexpr std::uint32_t max_gpu_draw_rows = 65536;
    constexpr std::uint32_t max_gpu_material_rows = 4096;
}

bool vulkan_backend::create_gpu_scene_tables()
{
    const auto transforms = runtime->create_buffer(render_graph::buffer_desc{
        .size = sizeof(glm::mat4) * max_gpu_draw_rows,
        .usage = render_graph::buffer_usage::STORAGE_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, transform_resource);
    const auto indirect = runtime->create_buffer(render_graph::buffer_desc{
        .size = sizeof(VkDrawIndexedIndirectCommand) * max_gpu_draw_rows,
        .usage = render_graph::buffer_usage::INDIRECT_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, indirect_resource);
    const auto materials = runtime->create_buffer(render_graph::buffer_desc{
        .size = sizeof(gpu_material_row) * max_gpu_material_rows,
        .usage = render_graph::buffer_usage::STORAGE_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, material_resource);
    if (!transforms || !indirect || !materials)
    {
        Logger::LogError("Failed to create GPU scene buffers: " +
                         (transforms ? indirect.error : transforms.error));
        return false;
    }
    transform_buffer = runtime->buffer(transform_resource);
    indirect_buffer = runtime->buffer(indirect_resource);
    material_buffer = runtime->buffer(material_resource);
    const auto slot = runtime->allocate_storage_buffer(transform_resource,
                                                        0,
                                                        sizeof(glm::mat4) * max_gpu_draw_rows,
                                                        transform_buffer_slot);
    if (!slot)
    {
        Logger::LogError("Failed to allocate GPU transform table slot: " + slot.error);
        return false;
    }
    const auto material_slot = runtime->allocate_storage_buffer(material_resource, 0,
                                                                 sizeof(gpu_material_row) * max_gpu_material_rows,
                                                                 material_buffer_slot);
    if (!material_slot)
    {
        Logger::LogError("Failed to allocate GPU material table slot: " + material_slot.error);
        return false;
    }
    material_rows.emplace_back();
    if (!runtime->update_buffer(material_resource, 0, std::as_bytes(std::span(material_rows)))) return false;
    return true;
}

engine::result<std::uint32_t> vulkan_backend::upload_materials(const engine::asset_database& asset)
{
    engine::result<std::uint32_t> result;
    if (material_rows.size() + asset.materials.size() > max_gpu_material_rows)
    {
        result.error = "GPU material table capacity exhausted";
        return result;
    }
    std::vector<std::uint32_t> image_slots(asset.images.size(), 0);
    for (std::uint32_t index = 0; index < asset.images.size(); index++)
    {
        const auto& source = asset.images[index];
        render_graph::vk_image_resource_handle image;
        const auto created = runtime->create_image(render_graph::image_desc{
            .fmt = render_graph::format::R8G8B8A8_UNORM,
            .extent = {source.width, source.height, 1},
            .usage = render_graph::image_usage::TRANSFER_DST | render_graph::image_usage::SAMPLED,
            .memory = render_graph::memory_domain::device_local,
            .aliasing = render_graph::aliasing_policy::forbidden,
            .lifetime = render_graph::resource_lifetime_class::persistent,
        }, image);
        if (!created || !runtime->stage_image_upload(image, 0, 0, {source.width, source.height, 1}, source.pixels))
        {
            result.error = created ? runtime->last_error() : created.error;
            return result;
        }
        render_graph::vk_bindless_handle slot;
        const auto bound = runtime->allocate_sampled_image(image, VK_FORMAT_R8G8B8A8_UNORM, slot);
        if (!bound)
        {
            result.error = bound.error;
            return result;
        }
        image_slots[index] = slot.index;
        texture_resources.push_back(image);
        texture_slots.push_back(slot);
    }
    std::vector<std::uint32_t> samplers(asset.samplers.size(), 0);
    const auto address = [](engine::sampler_wrap value)
    {
        if (value == engine::sampler_wrap::clamp_to_edge) return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (value == engine::sampler_wrap::mirrored_repeat) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };
    for (std::uint32_t index = 0; index < asset.samplers.size(); index++)
    {
        const auto& source = asset.samplers[index];
        render_graph::vk_bindless_handle slot;
        const auto created = runtime->create_sampler(render_graph::vk_sampler_desc{
            .min_filter = source.min_filter == engine::sampler_filter::nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
            .mag_filter = source.mag_filter == engine::sampler_filter::nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
            .address_u = address(source.wrap_u),
            .address_v = address(source.wrap_v),
        }, slot);
        if (!created)
        {
            result.error = created.error;
            return result;
        }
        samplers[index] = slot.index;
        sampler_slots.push_back(slot);
    }
    const auto image_slot = [&](const engine::texture_ref& ref, std::uint32_t fallback = 0u)
    { return ref.image < image_slots.size() ? image_slots[ref.image] : fallback; };
    const auto sampler_slot = [&](const engine::texture_ref& ref)
    { return ref.sampler < samplers.size() ? samplers[ref.sampler] : 0u; };

    result.value = static_cast<std::uint32_t>(material_rows.size());
    for (const auto& source : asset.materials)
    {
        const float alpha = source.alpha == engine::alpha_mode::mask ? 1.0F
                            : source.alpha == engine::alpha_mode::blend ? 2.0F : 0.0F;
        material_rows.push_back(gpu_material_row{
            .base_color = source.base_color_factor,
            .emissive_metallic = {source.emissive_factor, source.metallic_factor},
            .roughness_alpha = {source.roughness_factor, source.alpha_cutoff, alpha,
                                source.double_sided ? 1.0F : 0.0F},
            .texture_scales = {source.normal_texture.scale, source.occlusion_texture.scale, 0.0F, 0.0F},
            .image_slots = {image_slot(source.base_color_texture), image_slot(source.metallic_roughness_texture),
                            image_slot(source.normal_texture, 1), image_slot(source.occlusion_texture)},
            .sampler_slots = {sampler_slot(source.base_color_texture), sampler_slot(source.metallic_roughness_texture),
                              sampler_slot(source.normal_texture), sampler_slot(source.occlusion_texture)},
            .texcoords = {source.base_color_texture.texcoord, source.metallic_roughness_texture.texcoord,
                          source.normal_texture.texcoord, source.occlusion_texture.texcoord},
            .emissive_texture = {image_slot(source.emissive_texture), sampler_slot(source.emissive_texture),
                                 source.emissive_texture.texcoord, 0},
        });
    }
    const auto bytes = std::as_bytes(std::span(material_rows).subspan(result.value));
    if (!runtime->update_buffer(material_resource, sizeof(gpu_material_row) * result.value, bytes))
    {
        result.error = runtime->last_error();
        return result;
    }
    return result;
}

bool vulkan_backend::update_gpu_scene_tables()
{
    assert(current_packet != nullptr && !current_packet->camera_rows.empty());
    struct draw_candidate
    {
        glm::mat4 model{1.0F};
        engine::draw_range range;
        float distance_squared = 0.0F;
    };
    std::array<std::vector<draw_candidate>, 4> groups;
    const glm::vec3 camera_position = glm::vec3(glm::inverse(current_packet->camera_rows.front().view)[3]);
    std::size_t candidate_count = 0;
    for (const engine::instance_row& instance : current_packet->instance_rows)
    {
        if (instance.transform >= current_packet->transform_rows.size()) continue;
        const glm::mat4& model = current_packet->transform_rows[instance.transform];
        const auto allocation = geometry_allocations.find(instance.mesh);
        if (allocation == geometry_allocations.end()) continue;
        for (const engine::draw_range& range : allocation->second.draws)
        {
            if (++candidate_count > max_gpu_draw_rows)
            {
                Logger::LogError("GPU draw table capacity exhausted");
                return false;
            }
            const auto material_index = range.material_index < material_rows.size() ? range.material_index : 0u;
            const auto& material = material_rows[material_index];
            const bool blend = material.roughness_alpha.z == 2.0F;
            const bool double_sided = material.roughness_alpha.w != 0.0F;
            const std::uint32_t group = (blend ? 2u : 0u) + (double_sided ? 1u : 0u);
            const glm::vec3 position = glm::vec3(model[3]);
            groups[group].push_back({model, range, glm::dot(position - camera_position, position - camera_position)});
        }
    }
    for (std::uint32_t group = 2; group < groups.size(); group++)
        std::stable_sort(groups[group].begin(), groups[group].end(), [](const auto& left, const auto& right)
        { return left.distance_squared > right.distance_squared; });

    std::vector<gpu_transform_row> transforms;
    std::vector<VkDrawIndexedIndirectCommand> commands;
    indirect_group_counts = {};
    indirect_group_offsets = {};
    for (std::uint32_t group = 0; group < groups.size(); group++)
    {
        indirect_group_offsets[group] = static_cast<std::uint32_t>(commands.size());
        indirect_group_counts[group] = static_cast<std::uint32_t>(groups[group].size());
        for (const auto& candidate : groups[group])
        {
            const std::uint32_t draw_index = static_cast<std::uint32_t>(commands.size());
            transforms.push_back(gpu_transform_row{.model = candidate.model,
                                                   .metadata = {candidate.range.material_index, 0, 0, 0}});
            commands.push_back(VkDrawIndexedIndirectCommand{
                .indexCount = candidate.range.index_count,
                .instanceCount = 1,
                .firstIndex = candidate.range.first_index,
                .vertexOffset = candidate.range.vertex_offset,
                .firstInstance = draw_index,
            });
        }
    }
    indirect_draw_count = static_cast<std::uint32_t>(commands.size());
    if (commands.empty()) return true;
    const auto transform_bytes = std::as_bytes(std::span(transforms));
    const auto command_bytes = std::as_bytes(std::span(commands));
    if (!runtime->update_buffer(transform_resource, 0, transform_bytes) ||
        !runtime->update_buffer(indirect_resource, 0, command_bytes))
    {
        Logger::LogError("Failed to update GPU scene tables: " + runtime->last_error());
        return false;
    }
    return true;
}
