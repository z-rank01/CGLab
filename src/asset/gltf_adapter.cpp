#include "asset/gltf_adapter.h"

#include <utility>

#include <gltf/gltf_loader.h>

namespace asset
{
    engine::result<engine::asset_database> load_gltf(const std::filesystem::path& path)
    {
        // The glTF-to-row-table conversion lives in digital-content-loader; this
        // adapter only maps dcl::result onto the engine result type.
        auto loaded = dcl::load_gltf(path);
        engine::result<engine::asset_database> result;
        result.value = std::move(loaded.value);
        result.error = std::move(loaded.error);
        return result;
    }
} // namespace asset
