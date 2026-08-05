#include "smoke_scene.h"
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
    const smoke_scene_data scene = make_smoke_scene();
    CHECK(scene.vertices.size() == 3);
    CHECK(scene.indices.size() == 3);
    CHECK(scene.indices[0] == 0);
    CHECK(scene.indices[1] == 1);
    CHECK(scene.indices[2] == 2);
    CHECK(scene.draw_calls.size() == 1);
    CHECK(scene.draw_calls.front().index_count == 3);
    CHECK(scene.draw_calls.front().vertex_count == 3);
    CHECK(scene.meshes.size() == 1);
    CHECK(scene.meshes.front().primitives.size() == 1);
    CHECK(scene.meshes.front().primitives.front().index_count == 3);

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
