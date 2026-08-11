#include "asset/geometry_loader.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <tiny_gltf.h>

int main()
{
    const std::filesystem::path source = std::filesystem::path(CGLAB_SOURCE_DIR) / "assets" / "triangle.gltf";
    const auto check_geometry = [](const auto& result, const char* label, std::size_t expected_nodes = 1)
    {
        if (!result)
        {
            std::cerr << label << ": " << result.error << '\n';
            return false;
        }
        if (result.value.nodes.size() != expected_nodes || result.value.meshes.size() != 1 ||
            result.value.primitives.size() != 1 || result.value.vertex_blob.size() != 3 ||
            result.value.index_blob.size() != 3 || result.value.materials.size() != 1 ||
            result.value.nodes.front().mesh != 0 || result.value.primitives.front().material != 0)
        {
            std::cerr << label << ": unexpected triangle geometry shape\n";
            return false;
        }
        return true;
    };

    if (!check_geometry(asset::load_geometry(source), "ASCII glTF"))
    {
        return EXIT_FAILURE;
    }

    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    const std::filesystem::path uppercase_gltf = temp_dir / "cglab_triangle_asset_test.GLTF";
    const std::filesystem::path binary_glb = temp_dir / "cglab_triangle_asset_test.glb";
    const std::filesystem::path required_extension_gltf = temp_dir / "cglab_required_extension_test.gltf";
    std::error_code filesystem_error;
    std::filesystem::copy_file(source, uppercase_gltf,
                               std::filesystem::copy_options::overwrite_existing, filesystem_error);
    if (filesystem_error || !check_geometry(asset::load_geometry(uppercase_gltf), "Uppercase glTF"))
    {
        std::filesystem::remove(uppercase_gltf, filesystem_error);
        return EXIT_FAILURE;
    }

    tinygltf::TinyGLTF tiny_loader;
    tinygltf::Model model;
    std::string error;
    std::string warning;
    if (!tiny_loader.LoadASCIIFromFile(&model, &error, &warning, source.string()))
    {
        std::cerr << "Failed to load source fixture: " << error << '\n';
        return EXIT_FAILURE;
    }
    tinygltf::Node parent;
    parent.name = "Parent";
    parent.children = {0};
    parent.translation = {2.0, 0.0, 0.0};
    model.nodes.push_back(parent);
    tinygltf::Node instance;
    instance.name = "SecondInstance";
    instance.mesh = 0;
    instance.translation = {-2.0, 0.0, 0.0};
    model.nodes.push_back(instance);
    model.scenes[0].nodes = {1, 2};
    model.materials[0].alphaMode = "MASK";
    model.materials[0].alphaCutoff = 0.3;
    model.materials[0].doubleSided = true;
    if (
        !tiny_loader.WriteGltfSceneToFile(&model, binary_glb.string(), true, true, false, true) ||
        !check_geometry(asset::load_geometry(binary_glb), "Binary GLB", 3))
    {
        std::cerr << "Failed to create or load GLB fixture: " << error << '\n';
        std::filesystem::remove(uppercase_gltf, filesystem_error);
        std::filesystem::remove(binary_glb, filesystem_error);
        return EXIT_FAILURE;
    }
    const auto instanced = asset::load_geometry(binary_glb);
    if (!instanced || instanced.value.nodes[0].parent != 1 || instanced.value.nodes[2].mesh != 0 ||
        instanced.value.materials[0].alpha != engine::alpha_mode::mask ||
        !instanced.value.materials[0].double_sided)
    {
        std::cerr << "Hierarchy, instancing, or material rows were not preserved\n";
        return EXIT_FAILURE;
    }

    model.extensionsRequired = {"KHR_materials_clearcoat"};
    model.extensionsUsed = model.extensionsRequired;
    if (!tiny_loader.WriteGltfSceneToFile(&model, required_extension_gltf.string(), true, true, false, false))
        return EXIT_FAILURE;
    const auto required_extension = asset::load_geometry(required_extension_gltf);
    if (required_extension || required_extension.error.find("Unsupported required glTF extension") == std::string::npos)
    {
        std::cerr << "Required extension was not rejected\n";
        return EXIT_FAILURE;
    }

    const auto unsupported = asset::load_geometry(temp_dir / "model.obj");
    if (unsupported || unsupported.error.find("Unsupported geometry asset format") == std::string::npos)
    {
        std::cerr << "Unsupported asset format was not rejected\n";
        std::filesystem::remove(uppercase_gltf, filesystem_error);
        std::filesystem::remove(binary_glb, filesystem_error);
        return EXIT_FAILURE;
    }

    std::filesystem::remove(uppercase_gltf, filesystem_error);
    std::filesystem::remove(binary_glb, filesystem_error);
    std::filesystem::remove(required_extension_gltf, filesystem_error);
    return EXIT_SUCCESS;
}
