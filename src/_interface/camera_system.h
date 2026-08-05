#pragma once
#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <span>

#include "camera_component.h"
#include "input.h"
#include "input_router.h"

namespace interface
{
    // per-frame functions for camera system

    inline void reset_camera_update_context(camera_update_context& context)
    {
        context.mouse_delta_x  = 0.0F;
        context.mouse_delta_y  = 0.0F;
        context.scroll_delta_y = 0.0F;
    }

    // 薄封装：默认绑定表 + 路由层。保持原有签名与行为（cglab.camera_input 依赖）。
    inline void process_event(camera_update_context& ctx, const interface::input_event& event)
    {
        static const action_binding_table default_bindings = make_default_bindings();
        process_event_routed(ctx, event, default_bindings);
    }

    inline void recompute_camera_basis(camera_transform& transform)
    {
        glm::vec3 front;
        front.x         = cos(glm::radians(transform.yaw)) * cos(glm::radians(transform.pitch));
        front.y         = sin(glm::radians(transform.pitch));
        front.z         = sin(glm::radians(transform.yaw)) * cos(glm::radians(transform.pitch));
        transform.front = glm::normalize(front);
        transform.right = glm::normalize(glm::cross(transform.front, transform.world_up));
        transform.up    = glm::normalize(glm::cross(transform.right, transform.front));
        transform.dirty = false;
    }

    // --- fly 模式（原有行为，保持不变） ---

    inline void update_camera_fly(camera_transform& transform,
                                  const camera_config& config,
                                  const camera_update_context& ctx,
                                  float delta_time)
    {
        // rotation (Free Look)
        if (ctx.is_free_look_active && (std::abs(ctx.mouse_delta_x) > 0.0001F || std::abs(ctx.mouse_delta_y) > 0.0001F))
        {
            transform.yaw += ctx.mouse_delta_x * config.mouse_sensitivity;
            transform.pitch -= ctx.mouse_delta_y * config.mouse_sensitivity; // reverse Y
            transform.pitch = std::min(transform.pitch, 89.0F);
            transform.pitch = std::max(transform.pitch, -89.0F);
            transform.dirty = true;
        }

        // zoom
        if (std::abs(ctx.scroll_delta_y) > 0.0001F)
        {
            transform.current_zoom -= ctx.scroll_delta_y * config.zoom_speed;
            transform.current_zoom = std::max(transform.current_zoom, config.min_fov);
            transform.current_zoom = std::min(transform.current_zoom, config.max_fov);
            transform.dirty = true;
        }

        // update camera vectors if dirty
        if (transform.dirty)
        {
            recompute_camera_basis(transform);
        }

        // movement (free look mode)
        float velocity = config.movement_speed * delta_time;
        glm::vec3 move_dir(0.0F);
        if (ctx.move_forward)
            move_dir += transform.front;
        if (ctx.move_backward)
            move_dir -= transform.front;
        if (ctx.move_right)
            move_dir += transform.right;
        if (ctx.move_left)
            move_dir -= transform.right;
        if (ctx.move_up)
            move_dir += transform.world_up;
        if (ctx.move_down)
            move_dir -= transform.world_up;

        if (glm::length(move_dir) > 0.0F)
        {
            transform.position += glm::normalize(move_dir) * velocity;
        }

        // movement (pan mode)
        if (ctx.is_panning_active)
        {
            transform.position -= transform.right * ctx.mouse_delta_x * config.mouse_sensitivity * 0.1F;
            transform.position += transform.up * ctx.mouse_delta_y * config.mouse_sensitivity * 0.1F;
        }
    }

    // --- orbit 模式：环绕 focus_point ---

    inline void update_camera_orbit(camera_transform& transform,
                                    const camera_config& config,
                                    const camera_update_context& ctx,
                                    float /*delta_time*/)
    {
        // rotate（左键拖拽）
        if (ctx.is_orbit_look_active &&
            (std::abs(ctx.mouse_delta_x) > 0.0001F || std::abs(ctx.mouse_delta_y) > 0.0001F))
        {
            transform.yaw += ctx.mouse_delta_x * config.mouse_sensitivity;
            transform.pitch -= ctx.mouse_delta_y * config.mouse_sensitivity;
            transform.pitch = std::min(transform.pitch, 89.0F);
            transform.pitch = std::max(transform.pitch, -89.0F);
            transform.dirty = true;
        }

        // dolly（滚轮拉近/拉远焦点距离）
        if (std::abs(ctx.scroll_delta_y) > 0.0001F)
        {
            transform.orbit_distance -= ctx.scroll_delta_y * config.zoom_speed;
            transform.orbit_distance = std::max(transform.orbit_distance, config.min_focus_dist);
            transform.orbit_distance = std::min(transform.orbit_distance, config.max_focus_dist);
            transform.dirty = true;
        }

        if (transform.dirty)
        {
            recompute_camera_basis(transform);
        }

        // pan（中键平移焦点）
        if (ctx.is_panning_active)
        {
            transform.focus_point -= transform.right * ctx.mouse_delta_x * config.mouse_sensitivity * 0.1F;
            transform.focus_point += transform.up * ctx.mouse_delta_y * config.mouse_sensitivity * 0.1F;
        }

        // 位置由焦点与距离唯一决定，视线始终指向焦点
        transform.position = transform.focus_point - transform.front * transform.orbit_distance;
    }

    // --- bookmark 机位书签 ---

    inline camera_pose capture_camera_pose(const camera_transform& transform)
    {
        return camera_pose{
            .position     = transform.position,
            .yaw          = transform.yaw,
            .pitch        = transform.pitch,
            .current_zoom = transform.current_zoom,
        };
    }

    inline void apply_camera_pose(camera_transform& transform, const camera_pose& pose)
    {
        transform.position     = pose.position;
        transform.yaw          = pose.yaw;
        transform.pitch        = pose.pitch;
        transform.current_zoom = pose.current_zoom;
        transform.dirty        = true;
    }

    inline bool save_bookmark(camera_container& container, size_t camera_index, size_t slot)
    {
        if (camera_index >= container.transforms.size() || slot >= bookmark_slot_count)
        {
            return false;
        }
        container.bookmarks[camera_index].poses[slot] = capture_camera_pose(container.transforms[camera_index]);
        container.bookmarks[camera_index].valid[slot] = true;
        return true;
    }

    // 跳转到已保存机位：启动 smoothstep 混合。槽位为空返回 false。
    inline bool goto_bookmark(camera_container& container, size_t camera_index, size_t slot)
    {
        if (camera_index >= container.transforms.size() || slot >= bookmark_slot_count ||
            !container.bookmarks[camera_index].valid[slot])
        {
            return false;
        }
        camera_blend_state& blend = container.blends[camera_index];
        blend.from                = capture_camera_pose(container.transforms[camera_index]);
        blend.to                  = container.bookmarks[camera_index].poses[slot];
        blend.elapsed             = 0.0F;
        blend.active              = true;
        return true;
    }

    // 推进混合。返回 true 表示本帧由混合接管（调用方应跳过模式更新）。
    inline bool update_camera_blend(camera_transform& transform, camera_blend_state& blend, float delta_time)
    {
        if (!blend.active)
        {
            return false;
        }
        blend.elapsed += delta_time;
        const float t         = std::min(blend.elapsed / blend.duration, 1.0F);
        const float smoothed  = t * t * (3.0F - 2.0F * t); // smoothstep
        transform.position    = glm::mix(blend.from.position, blend.to.position, smoothed);
        transform.yaw         = glm::mix(blend.from.yaw, blend.to.yaw, smoothed);
        transform.pitch       = glm::mix(blend.from.pitch, blend.to.pitch, smoothed);
        transform.current_zoom = glm::mix(blend.from.current_zoom, blend.to.current_zoom, smoothed);
        recompute_camera_basis(transform);
        if (t >= 1.0F)
        {
            blend.active = false;
            apply_camera_pose(transform, blend.to); // 消除浮点尾差
        }
        return true;
    }

    // --- 模式切换：保持视角连续 ---

    inline void set_camera_mode(camera_container& container, size_t camera_index, camera_mode mode)
    {
        camera_transform& transform = container.transforms[camera_index];
        if (transform.mode == mode)
        {
            return;
        }
        if (mode == camera_mode::orbit)
        {
            // 以当前注视方向 orbit_distance 处作为焦点，切换瞬间画面不动
            transform.focus_point = transform.position + transform.front * transform.orbit_distance;
        }
        transform.mode  = mode;
        transform.dirty = true;
    }

    // --- 每帧更新入口 ---

    inline void update_camera(camera_container& container, const camera_update_context& ctx, float delta_time)
    {
        for (size_t i = 0; i < container.transforms.size(); ++i)
        {
            auto& transform     = container.transforms[i];
            const auto& config  = container.configs[i];

            // bookmark 混合期间屏蔽模式输入
            if (update_camera_blend(transform, container.blends[i], delta_time))
            {
                continue;
            }

            switch (transform.mode)
            {
            case camera_mode::fly:
                update_camera_fly(transform, config, ctx, delta_time);
                break;
            case camera_mode::orbit:
                update_camera_orbit(transform, config, ctx, delta_time);
                break;
            }
        }
    }

    inline void tick(
        camera_container& container,
        camera_update_context& context,
        const interface::input_event& event,
        const float delta_time)
    {
        reset_camera_update_context(context);
        process_event(context, event);
        update_camera(container, context, delta_time);
    }

    inline void tick(
        camera_container& container,
        camera_update_context& context,
        std::span<const input_event> events,
        const float delta_time)
    {
        reset_camera_update_context(context);
        for (const input_event& event : events)
        {
            process_event(context, event);
        }
        update_camera(container, context, delta_time);
    }

    // functions to get view and projection matrices

    inline glm::mat4 get_view_matrix(const camera_transform& transform)
    {
        return glm::lookAt(transform.position, transform.position + transform.front, transform.up);
    }

    inline glm::mat4 get_projection_matrix(const camera_transform& transform, const camera_config& config)
    {
        return glm::perspective(glm::radians(transform.current_zoom), config.aspect_ratio, config.near_plane, config.far_plane);
    }
} // namespace interface
