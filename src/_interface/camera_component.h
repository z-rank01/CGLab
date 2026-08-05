#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace interface
{
    // Hot Data Block
    // 1. frequently accessed data
    // 2. update together most of the time
    struct camera_transform
    {
        glm::vec3 position{0.0F, 0.0F, 10.0F};
        glm::vec3 front{0.0F, 0.0F, -1.0F};
        glm::vec3 up{0.0F, 1.0F, 0.0F};
        glm::vec3 right{1.0F, 0.0F, 0.0F};
        glm::vec3 world_up{0.0F, 1.0F, 0.0F};
        glm::vec3 focus_point{0.0F};
        float yaw          = -90.0F;
        float pitch        = 0.0F;
        float current_zoom = 45.0F;
        bool dirty         = true;      // indicates if the camera vectors need to be updated
        char pad[3]        = {0};   // padding for alignment
    };

    // Cold Data Block
    // 1. infrequently accessed data
    // 2. set up together most of the time
    struct camera_config
    {
        float fov               = 45.0F;
        float aspect_ratio      = 16.0F / 9.0F;
        float near_plane        = 0.1F;
        float far_plane         = 1000.0F;
        float movement_speed    = 10.0F;
        float mouse_sensitivity = 0.1F;
        float zoom_speed        = 2.0F;
        float min_focus_dist    = 0.5F;
        float max_focus_dist    = 100.0F;
    };

    // Context used during camera update per frame
    struct camera_update_context
    {
        // keyboard related states
        bool move_forward  = false; // W
        bool move_backward = false; // S
        bool move_left     = false; // A
        bool move_right    = false; // D
        bool move_up       = false; // E / Space
        bool move_down     = false; // Q / LCtrl

        // mouse related states
        bool is_free_look_active = false; // right button pressed
        bool is_panning_active   = false; // middle button pressed

        // accumulated deltas (need to be reset at the start of each frame)
        float mouse_delta_x  = 0.0F;
        float mouse_delta_y  = 0.0F;
        float scroll_delta_y = 0.0F;
    };

    // Container to hold all camera data blocks
    struct camera_container
    {
        std::vector<camera_transform> transforms;
        std::vector<camera_config> configs;

        // use size_t as index to identify cameras
        // because size_t is large enough to hold all possible indices
        size_t add_camera(const camera_transform& entity = camera_transform(), const camera_config& config = camera_config())
        {
            transforms.push_back(entity);
            configs.push_back(config);
            return transforms.size() - 1;
        }
    };
} // namespace dod_camera
