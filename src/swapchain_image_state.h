#pragma once

#include <cstddef>
#include <vector>

class swapchain_image_state_tracker
{
public:
    void reset(std::size_t image_count) { initialized_.assign(image_count, false); }

    [[nodiscard]] bool is_initialized(std::size_t image_index) const noexcept
    {
        return image_index < initialized_.size() && initialized_[image_index];
    }

    void mark_presented(std::size_t image_index) noexcept
    {
        if (image_index < initialized_.size())
        {
            initialized_[image_index] = true;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return initialized_.size(); }

private:
    std::vector<bool> initialized_;
};
