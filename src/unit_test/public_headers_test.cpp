#include "engine/geometry.h"
#include "engine/render_backend.h"
#include "engine/engine_runtime.h"
#include "engine/runtime_config.h"

#if defined(VK_VERSION_1_0) || defined(SDL_MAJOR_VERSION) || defined(TINYGLTF_VERSION_MAJOR)
#error "engine public headers must not expose Vulkan, SDL, or tinygltf"
#endif

int main()
{
    engine::asset_database asset;
    engine::runtime_config config;
    return asset.name.empty() && config.frames_in_flight == 3 ? 0 : 1;
}
