#include "asset/geometry_loader.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <tiny_gltf.h>

int main()
{
    const std::filesystem::path source = std::filesystem::path(CGLAB_SOURCE_DIR) / "assets" / "triangle.gltf";
    const auto check_geometry = [](const auto& result, const char* label)
    {
        if (!result)
        {
            std::cerr << label << ": " << result.error << '\n';
            return false;
        }
        if (result.value.primitives.size() != 1 ||
            result.value.primitives.front().vertices.size() != 3 ||
            result.value.primitives.front().indices.size() != 3)
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
    if (!tiny_loader.LoadASCIIFromFile(&model, &error, &warning, source.string()) ||
        !tiny_loader.WriteGltfSceneToFile(&model, binary_glb.string(), true, true, false, true) ||
        !check_geometry(asset::load_geometry(binary_glb), "Binary GLB"))
    {
        std::cerr << "Failed to create or load GLB fixture: " << error << '\n';
        std::filesystem::remove(uppercase_gltf, filesystem_error);
        std::filesystem::remove(binary_glb, filesystem_error);
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
    return EXIT_SUCCESS;
}
