#include "engine/geometry.h"
#include "engine/render_backend.h"
#include "framework/engine_runtime.h"
#include "framework/runtime_config.h"

#if defined(VK_VERSION_1_0) || defined(SDL_MAJOR_VERSION) || defined(TINYGLTF_VERSION_MAJOR)
#error "engine/framework public headers must not expose Vulkan, SDL, or tinygltf"
#endif

int main()
{
    engine::geometry_asset asset;
    framework::runtime_config config;
    return asset.empty() && config.frames_in_flight == 3 ? 0 : 1;
}
