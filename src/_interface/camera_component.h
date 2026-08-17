#pragma once

#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace interface
{
    // 相机模式
    // - fly：自由飞行（右键 look + WASDQE，原有行为）
    // - orbit：环绕焦点（左键环绕、滚轮 dolly、中键 pan 焦点）
    enum class camera_mode : std::uint8_t
    {
        fly,
        orbit,
    };

    // 可序列化的相机位姿（bookmark 用）
    struct camera_pose
    {
        glm::vec3 position{0.0F, 0.0F, 10.0F};
        float yaw          = -90.0F;
        float pitch        = 0.0F;
        float current_zoom = 45.0F;
    };

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
        float yaw            = -90.0F;
        float pitch          = 0.0F;
        float current_zoom   = 45.0F;
        float orbit_distance = 10.0F; // orbit 模式：与 focus_point 的距离
        camera_mode mode     = camera_mode::fly;
        bool dirty           = true; // indicates if the camera vectors need to be updated
        char pad[2]          = {0};  // padding for alignment
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
        // fly 模式滚轮 zoom（即 FOV）允许范围；远程设置的 FOV 也会被钳到此区间
        float min_fov           = 1.0F;
        float max_fov           = 120.0F;
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
        bool is_free_look_active  = false; // right button pressed
        bool is_panning_active    = false; // middle button pressed
        bool is_orbit_look_active = false; // left button pressed（orbit 模式旋转）

        // accumulated deltas (need to be reset at the start of each frame)
        float mouse_delta_x  = 0.0F;
        float mouse_delta_y  = 0.0F;
        float scroll_delta_y = 0.0F;
    };

    // 机位书签（每相机 8 槽）
    inline constexpr std::size_t bookmark_slot_count = 8;

    struct camera_bookmarks
    {
        std::array<camera_pose, bookmark_slot_count> poses{};
        std::array<bool, bookmark_slot_count> valid{};
    };

    // bookmark 跳转的插值状态（smoothstep 混合，跳转期间屏蔽输入）
    struct camera_blend_state
    {
        bool active       = false;
        float elapsed     = 0.0F;
        float duration    = 0.6F;
        camera_pose from{};
        camera_pose to{};
    };

    // Container to hold all camera data blocks
    struct camera_container
    {
        std::vector<camera_transform> transforms;
        std::vector<camera_config> configs;
        std::vector<camera_bookmarks> bookmarks;
        std::vector<camera_blend_state> blends;

        // use size_t as index to identify cameras
        // because size_t is large enough to hold all possible indices
        size_t add_camera(const camera_transform& entity = camera_transform(), const camera_config& config = camera_config())
        {
            transforms.push_back(entity);
            configs.push_back(config);
            bookmarks.emplace_back();
            blends.emplace_back();
            return transforms.size() - 1;
        }
    };
} // namespace interface
