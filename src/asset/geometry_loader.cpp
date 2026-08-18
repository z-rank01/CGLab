#include "asset/geometry_loader.h"

#include "asset/gltf_adapter.h"

namespace asset
{
    engine::result<engine::asset_database> load_geometry(const std::filesystem::path& path,
                                                         engine::load_report* report)
    {
        // 格式分发已收敛进 dcl::load_asset（gltf_adapter 转调 + 三段计时映射）。
        return load_asset(path, report);
    }
} // namespace asset
