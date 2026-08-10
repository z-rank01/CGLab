#include "asset/geometry_loader.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "asset/gltf_adapter.h"

namespace asset
{
    engine::result<engine::geometry_asset> load_geometry(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

        if (extension == ".gltf" || extension == ".glb")
        {
            return load_gltf(path);
        }

        engine::result<engine::geometry_asset> result;
        result.error = "Unsupported geometry asset format: " +
                       (extension.empty() ? std::string("<none>") : extension);
        return result;
    }
} // namespace asset
