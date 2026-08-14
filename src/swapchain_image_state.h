#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class swapchain_image_state_tracker
{
public:
    void reset(std::size_t image_count) { initialized_.assign(image_count, 0); }

    [[nodiscard]] bool is_initialized(std::size_t image_index) const noexcept
    {
        return image_index < initialized_.size() && initialized_[image_index] != 0;
    }

    void mark_presented(std::size_t image_index) noexcept
    {
        if (image_index < initialized_.size())
        {
            initialized_[image_index] = 1;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return initialized_.size(); }

private:
    // uint8 列：DoD 契约禁用位打包代理容器（见 ArchitectureContract.cmake）
    std::vector<std::uint8_t> initialized_;
};
