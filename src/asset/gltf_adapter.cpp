#include "asset/gltf_adapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <tiny_gltf.h>

namespace
{
    std::size_t component_size(int type)
    {
        switch (type)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
        case TINYGLTF_COMPONENT_TYPE_DOUBLE: return 8;
        default: return 0;
        }
    }

    std::size_t component_count(int type)
    {
        switch (type)
        {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        default: return 0;
        }
    }

    double read_component(const unsigned char* data, int type, bool normalized)
    {
        switch (type)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        {
            std::int8_t value{}; std::memcpy(&value, data, sizeof(value));
            return normalized ? std::max(-1.0, static_cast<double>(value) / 127.0) : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            std::uint8_t value{}; std::memcpy(&value, data, sizeof(value));
            return normalized ? static_cast<double>(value) / 255.0 : value;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        {
            std::int16_t value{}; std::memcpy(&value, data, sizeof(value));
            return normalized ? std::max(-1.0, static_cast<double>(value) / 32767.0) : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            std::uint16_t value{}; std::memcpy(&value, data, sizeof(value));
            return normalized ? static_cast<double>(value) / 65535.0 : value;
        }
        case TINYGLTF_COMPONENT_TYPE_INT:
        {
            std::int32_t value{}; std::memcpy(&value, data, sizeof(value)); return value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            std::uint32_t value{}; std::memcpy(&value, data, sizeof(value)); return value;
        }
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
        {
            float value{}; std::memcpy(&value, data, sizeof(value)); return value;
        }
        case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        {
            double value{}; std::memcpy(&value, data, sizeof(value)); return value;
        }
        default: return 0.0;
        }
    }

    bool accessor_values(const tinygltf::Model& model, int index, std::size_t wanted_components,
                         std::vector<double>& values, std::string& error)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= model.accessors.size())
        {
            error = "Invalid glTF accessor index";
            return false;
        }
        const auto& accessor = model.accessors[index];
        if (accessor.bufferView < 0 || static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size())
        {
            error = "Sparse or missing glTF bufferView is unsupported";
            return false;
        }
        const auto& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size())
        {
            error = "Invalid glTF buffer index";
            return false;
        }
        const std::size_t source_components = component_count(accessor.type);
        const std::size_t scalar_size = component_size(accessor.componentType);
        if (source_components == 0 || scalar_size == 0 || source_components < wanted_components)
        {
            error = "Unsupported glTF accessor type";
            return false;
        }
        const auto& buffer = model.buffers[view.buffer].data;
        const std::size_t stride = accessor.ByteStride(view) > 0
                                       ? static_cast<std::size_t>(accessor.ByteStride(view))
                                       : scalar_size * source_components;
        const std::size_t start = view.byteOffset + accessor.byteOffset;
        if (accessor.count > 0 && start + stride * (accessor.count - 1) + scalar_size * source_components > buffer.size())
        {
            error = "glTF accessor exceeds its buffer";
            return false;
        }
        values.resize(accessor.count * wanted_components);
        for (std::size_t row = 0; row < accessor.count; row++)
            for (std::size_t component = 0; component < wanted_components; component++)
                values[row * wanted_components + component] =
                    read_component(buffer.data() + start + row * stride + component * scalar_size,
                                   accessor.componentType, accessor.normalized);
        return true;
    }

    glm::mat4 local_transform(const tinygltf::Node& node)
    {
        if (node.matrix.size() == 16)
        {
            glm::mat4 result{1.0F};
            for (std::size_t column = 0; column < 4; column++)
                for (std::size_t row = 0; row < 4; row++)
                    result[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
            return result;
        }
        glm::vec3 translation{0.0F};
        glm::vec3 scale{1.0F};
        glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
        if (node.translation.size() == 3)
            translation = {node.translation[0], node.translation[1], node.translation[2]};
        if (node.scale.size() == 3) scale = {node.scale[0], node.scale[1], node.scale[2]};
        if (node.rotation.size() == 4)
            rotation = glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                 static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        return glm::translate(glm::mat4(1.0F), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0F), scale);
    }

    engine::sampler_filter filter(int value)
    {
        return value == TINYGLTF_TEXTURE_FILTER_NEAREST || value == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
               value == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR
                   ? engine::sampler_filter::nearest : engine::sampler_filter::linear;
    }

    engine::sampler_wrap wrap(int value)
    {
        if (value == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) return engine::sampler_wrap::clamp_to_edge;
        if (value == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) return engine::sampler_wrap::mirrored_repeat;
        return engine::sampler_wrap::repeat;
    }

    engine::texture_ref texture_ref(const tinygltf::Model& model, int texture_index, int texcoord, float scale = 1.0F)
    {
        engine::texture_ref result{.texcoord = static_cast<std::uint32_t>(std::max(0, texcoord)), .scale = scale};
        if (texture_index < 0 || static_cast<std::size_t>(texture_index) >= model.textures.size()) return result;
        const auto& texture = model.textures[texture_index];
        if (texture.source >= 0) result.image = static_cast<std::uint32_t>(texture.source);
        if (texture.sampler >= 0) result.sampler = static_cast<std::uint32_t>(texture.sampler);
        return result;
    }
}

namespace asset
{
    engine::result<engine::asset_database> load_gltf(const std::filesystem::path& path)
    {
        engine::result<engine::asset_database> result;
        std::error_code filesystem_error;
        if (!std::filesystem::is_regular_file(path, filesystem_error) || filesystem_error)
        {
            result.error = "Asset file does not exist: " + path.string();
            return result;
        }
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string error;
        std::string warning;
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
        { return static_cast<char>(std::tolower(character)); });
        const bool loaded = extension == ".glb"
                                ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
                                : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
        if (!loaded)
        {
            result.error = "Failed to load glTF asset: " + error;
            return result;
        }
        if (!model.extensionsRequired.empty())
        {
            result.error = "Unsupported required glTF extension: " + model.extensionsRequired.front();
            return result;
        }

        engine::asset_database output;
        output.name = path.filename().string();
        if (!warning.empty()) output.warnings.push_back(warning);
        for (const auto& optional : model.extensionsUsed)
            output.warnings.push_back("Ignored optional glTF extension: " + optional);

        output.samplers.reserve(model.samplers.size());
        for (const auto& source : model.samplers)
            output.samplers.push_back({filter(source.minFilter), filter(source.magFilter), wrap(source.wrapS), wrap(source.wrapT)});
        output.images.reserve(model.images.size());
        for (const auto& source : model.images)
        {
            if (source.width <= 0 || source.height <= 0 || source.image.empty())
            {
                result.error = "glTF image decode produced empty pixels";
                return result;
            }
            engine::image_row image{.name = source.name,
                                    .width = static_cast<std::uint32_t>(source.width),
                                    .height = static_cast<std::uint32_t>(source.height)};
            image.pixels.resize(static_cast<std::size_t>(source.width) * source.height * 4);
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(source.width) * source.height; pixel++)
            {
                const int components = std::max(1, source.component);
                const auto channel = [&](int index) -> std::byte
                {
                    if (index >= components) return index == 3 ? std::byte{255} : std::byte{0};
                    return static_cast<std::byte>(source.image[pixel * components + index]);
                };
                image.pixels[pixel * 4 + 0] = channel(0);
                image.pixels[pixel * 4 + 1] = components == 1 ? channel(0) : channel(1);
                image.pixels[pixel * 4 + 2] = components <= 2 ? channel(0) : channel(2);
                image.pixels[pixel * 4 + 3] = components == 2 ? channel(1) : channel(3);
            }
            output.images.push_back(std::move(image));
        }

        output.materials.reserve(model.materials.size());
        for (const auto& source : model.materials)
        {
            engine::material_row material{.name = source.name};
            const auto& pbr = source.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() == 4)
                material.base_color_factor = {pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3]};
            material.metallic_factor = static_cast<float>(pbr.metallicFactor);
            material.roughness_factor = static_cast<float>(pbr.roughnessFactor);
            material.base_color_texture = texture_ref(model, pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord);
            material.metallic_roughness_texture = texture_ref(model, pbr.metallicRoughnessTexture.index, pbr.metallicRoughnessTexture.texCoord);
            material.normal_texture = texture_ref(model, source.normalTexture.index, source.normalTexture.texCoord,
                                                  static_cast<float>(source.normalTexture.scale));
            material.occlusion_texture = texture_ref(model, source.occlusionTexture.index, source.occlusionTexture.texCoord,
                                                     static_cast<float>(source.occlusionTexture.strength));
            material.emissive_texture = texture_ref(model, source.emissiveTexture.index, source.emissiveTexture.texCoord);
            if (source.emissiveFactor.size() == 3)
                material.emissive_factor = {source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]};
            material.alpha = source.alphaMode == "MASK" ? engine::alpha_mode::mask
                             : source.alphaMode == "BLEND" ? engine::alpha_mode::blend : engine::alpha_mode::opaque;
            material.alpha_cutoff = static_cast<float>(source.alphaCutoff);
            material.double_sided = source.doubleSided;
            output.materials.push_back(std::move(material));
        }
        if (output.materials.empty()) output.materials.emplace_back();

        output.meshes.reserve(model.meshes.size());
        for (std::uint32_t mesh_index = 0; mesh_index < model.meshes.size(); mesh_index++)
        {
            const auto& source_mesh = model.meshes[mesh_index];
            engine::mesh_row mesh{.name = source_mesh.name,
                                  .first_primitive = static_cast<std::uint32_t>(output.primitives.size()),
                                  .bounds_min = glm::vec3(std::numeric_limits<float>::max()),
                                  .bounds_max = glm::vec3(std::numeric_limits<float>::lowest())};
            for (const auto& source : source_mesh.primitives)
            {
                if (source.mode != TINYGLTF_MODE_TRIANGLES)
                {
                    result.error = "Unsupported glTF primitive mode; only TRIANGLES is supported";
                    return result;
                }
                const auto position = source.attributes.find("POSITION");
                if (position == source.attributes.end())
                {
                    result.error = "glTF primitive is missing POSITION";
                    return result;
                }
                std::vector<double> positions;
                if (!accessor_values(model, position->second, 3, positions, result.error)) return result;
                const std::uint32_t vertex_count = static_cast<std::uint32_t>(positions.size() / 3);
                const std::uint32_t vertex_offset = static_cast<std::uint32_t>(output.vertex_blob.size());
                output.vertex_blob.resize(output.vertex_blob.size() + vertex_count);
                const auto read_attribute = [&](const char* name, std::size_t count, std::vector<double>& values)
                {
                    const auto found = source.attributes.find(name);
                    return found == source.attributes.end() || accessor_values(model, found->second, count, values, result.error);
                };
                std::vector<double> normals, tangents, colors, uv0, uv1;
                if (!read_attribute("NORMAL", 3, normals) || !read_attribute("TANGENT", 4, tangents) ||
                    !read_attribute("COLOR_0", 3, colors) || !read_attribute("TEXCOORD_0", 2, uv0) ||
                    !read_attribute("TEXCOORD_1", 2, uv1)) return result;
                for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; vertex_index++)
                {
                    auto& vertex = output.vertex_blob[vertex_offset + vertex_index];
                    vertex.position = {positions[vertex_index * 3], positions[vertex_index * 3 + 1], positions[vertex_index * 3 + 2]};
                    if (!normals.empty()) vertex.normal = {normals[vertex_index * 3], normals[vertex_index * 3 + 1], normals[vertex_index * 3 + 2]};
                    if (!tangents.empty()) vertex.tangent = {tangents[vertex_index * 4], tangents[vertex_index * 4 + 1], tangents[vertex_index * 4 + 2], tangents[vertex_index * 4 + 3]};
                    if (!colors.empty()) vertex.color = {colors[vertex_index * 3], colors[vertex_index * 3 + 1], colors[vertex_index * 3 + 2], 1.0F};
                    if (!uv0.empty()) vertex.uv0 = {uv0[vertex_index * 2], uv0[vertex_index * 2 + 1]};
                    if (!uv1.empty()) vertex.uv1 = {uv1[vertex_index * 2], uv1[vertex_index * 2 + 1]};
                    mesh.bounds_min = glm::min(mesh.bounds_min, vertex.position);
                    mesh.bounds_max = glm::max(mesh.bounds_max, vertex.position);
                }
                const std::uint32_t index_offset = static_cast<std::uint32_t>(output.index_blob.size());
                if (source.indices >= 0)
                {
                    std::vector<double> indices;
                    if (!accessor_values(model, source.indices, 1, indices, result.error)) return result;
                    for (double index : indices) output.index_blob.push_back(static_cast<std::uint32_t>(index));
                }
                else
                    for (std::uint32_t index = 0; index < vertex_count; index++) output.index_blob.push_back(index);
                output.primitives.push_back({.mesh = mesh_index,
                                             .material = source.material >= 0 ? static_cast<std::uint32_t>(source.material) : 0,
                                             .vertex_offset = vertex_offset,
                                             .vertex_count = vertex_count,
                                             .index_offset = index_offset,
                                             .index_count = static_cast<std::uint32_t>(output.index_blob.size()) - index_offset});
            }
            mesh.primitive_count = static_cast<std::uint32_t>(output.primitives.size()) - mesh.first_primitive;
            output.meshes.push_back(std::move(mesh));
        }

        output.nodes.resize(model.nodes.size());
        for (std::uint32_t index = 0; index < model.nodes.size(); index++)
        {
            const auto& source = model.nodes[index];
            output.nodes[index].name = source.name;
            output.nodes[index].mesh = source.mesh >= 0 ? static_cast<std::uint32_t>(source.mesh) : engine::invalid_asset_index;
            output.nodes[index].local_transform = local_transform(source);
            for (int child : source.children)
            {
                if (child < 0 || static_cast<std::size_t>(child) >= output.nodes.size())
                {
                    result.error = "glTF node contains invalid child index";
                    return result;
                }
                output.nodes[child].parent = index;
            }
        }
        // Reject cyclic or malformed node hierarchies on the worker side so the
        // main thread never walks an unbounded parent chain.
        for (std::uint32_t index = 0; index < output.nodes.size(); index++)
        {
            std::uint32_t cursor = index;
            for (std::size_t depth = 0; depth <= output.nodes.size(); depth++)
            {
                const auto parent = output.nodes[cursor].parent;
                if (parent == engine::invalid_asset_index) break;
                if (parent >= output.nodes.size())
                {
                    result.error = "glTF node contains invalid parent index";
                    return result;
                }
                if (depth == output.nodes.size())
                {
                    result.error = "glTF node hierarchy contains a cycle";
                    return result;
                }
                cursor = parent;
            }
        }
        if (output.primitives.empty())
        {
            result.error = "Asset contains no drawable primitives: " + path.string();
            return result;
        }
        result.value = std::move(output);
        return result;
    }
} // namespace asset
