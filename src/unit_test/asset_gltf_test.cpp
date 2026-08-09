#include "asset/gltf_adapter.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main()
{
    const auto result = asset::load_gltf(std::filesystem::path(CGLAB_SOURCE_DIR) / "assets" / "triangle.gltf");
    if (!result)
    {
        std::cerr << result.error << '\n';
        return EXIT_FAILURE;
    }
    if (result.value.primitives.size() != 1 ||
        result.value.primitives.front().vertices.size() != 3 ||
        result.value.primitives.front().indices.size() != 3)
    {
        std::cerr << "Unexpected triangle geometry shape\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
