#include "asset/gltf_adapter.h"

#include <utility>

#include <asset_loader.h>

namespace asset
{
    engine::result<engine::asset_database> load_asset(const std::filesystem::path& path,
                                                      engine::load_report* report)
    {
        // The glTF-to-row-table conversion lives in digital-content-loader; this
        // adapter only maps dcl::result/load_report onto the engine types.
        dcl::load_report dcl_report;
        auto loaded = dcl::load_asset(path, {}, report != nullptr ? &dcl_report : nullptr);
        if (report != nullptr)
        {
            report->parse_us = dcl_report.parse_us;
            report->decode_us = dcl_report.decode_us;
            report->convert_us = dcl_report.convert_us;
        }
        engine::result<engine::asset_database> result;
        result.value = std::move(loaded.value);
        result.error = std::move(loaded.error);
        return result;
    }
} // namespace asset
