#include "asset/gltf_adapter.h"

#include <future>
#include <utility>
#include <vector>

#include <stb_image.h>

namespace
{
    // 单行 stb 解码（与 dcl 串行回退同口径：RGBA8 + 失败置 error + 释放编码字节）。
    void decode_one_image(dcl::image_decode_row& row)
    {
        if (!row.error.empty()) return;
        int width = 0;
        int height = 0;
        int components = 0;
        unsigned char* pixels = stbi_load_from_memory(row.bytes.data(), static_cast<int>(row.bytes.size()),
                                                      &width, &height, &components, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            row.error = "stb_image decode failed: " + std::string(stbi_failure_reason());
            return;
        }
        row.width = static_cast<std::uint32_t>(width);
        row.height = static_cast<std::uint32_t>(height);
        row.pixels.assign(pixels, pixels + (static_cast<std::size_t>(width) * height * 4));
        stbi_image_free(pixels);
        row.bytes.clear();
        row.bytes.shrink_to_fit();
    }

    // 批量解码：每图一个任务提交到池，本线程等待全部完成。
    // 嵌套提交安全（非窃取有界池）：调用方本身就是池 worker（asset_service 的
    // 解析任务）且在本函数内阻塞等待——若不加以约束，19 个 worker 全堵在这、
    // 解码任务无人消费即死锁。规避：asset_service 把在途解析任务节流到 ≤4，
    // 池中始终有 ≥15 个 worker 可消费解码子任务（详见 asset_service.cpp）。
    void decode_image_rows_parallel(void* state, dcl::image_decode_row* rows, std::size_t count)
    {
        auto& jobs = *static_cast<infra::job_system*>(state);
        std::vector<std::future<void>> futures;
        futures.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            futures.push_back(jobs.submit([&row = rows[index]] { decode_one_image(row); }));
        }
        for (auto& future : futures) future.get();
    }
} // namespace

namespace asset
{
    engine::result<engine::asset_database> load_asset(const std::filesystem::path& path,
                                                      engine::load_report* report,
                                                      const dcl::load_options& options)
    {
        // The glTF-to-row-table conversion lives in digital-content-loader; this
        // adapter only maps dcl::result/load_report onto the engine types.
        dcl::load_report dcl_report;
        auto loaded = dcl::load_asset(path, options, report != nullptr ? &dcl_report : nullptr);
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

    dcl::image_decode_executor make_parallel_image_decode_executor(infra::job_system& jobs) noexcept
    {
        return {.state = &jobs, .decode_batch = &decode_image_rows_parallel};
    }
} // namespace asset
