#include "swapchain_image_state.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void check(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    swapchain_image_state_tracker swapchain_states;
    swapchain_states.reset(3);
    CHECK(swapchain_states.size() == 3);
    CHECK(!swapchain_states.is_initialized(0));
    CHECK(!swapchain_states.is_initialized(3));
    swapchain_states.mark_presented(1);
    CHECK(!swapchain_states.is_initialized(0));
    CHECK(swapchain_states.is_initialized(1));
    swapchain_states.reset(2);
    CHECK(swapchain_states.size() == 2);
    CHECK(!swapchain_states.is_initialized(1));
    return EXIT_SUCCESS;
}
