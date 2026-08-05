#pragma once
#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <span>

#include "camera_component.h"
#include "input.h"

namespace interface
{
    // per-frame functions for camera system

    inline void reset_camera_update_context(camera_update_context& context)
    {
        context.mouse_delta_x  = 0.0F;
        context.mouse_delta_y  = 0.0F;
        context.scroll_delta_y = 0.0F;
    }

    inline void process_event(camera_update_context& ctx, const interface::input_event& event)
    {
        using namespace interface;

        switch (event.type)
        {
        case event_type::key_down:
        case event_type::key_up:
        {
            bool is_down = (event.type == event_type::key_down);
            switch (event.key.key)
            {
            case key_code::w:
                ctx.move_forward = is_down;
                break;
            case key_code::s:
                ctx.move_backward = is_down;
                break;
            case key_code::a:
                ctx.move_left = is_down;
                break;
            case key_code::d:
                ctx.move_right = is_down;
                break;
            case key_code::e:
            case key_code::space:
                ctx.move_up = is_down;
                break;
            case key_code::q:
            case key_code::lctrl:
                ctx.move_down = is_down;
                break;
            default:
                break;
            }
            break;
        }
        case event_type::mouse_button_down:
        case event_type::mouse_button_up:
        {
            bool is_down = (event.type == event_type::mouse_button_down);
            if (event.mouse_button.button == mouse_button::right)
                ctx.is_free_look_active = is_down;
            if (event.mouse_button.button == mouse_button::middle)
                ctx.is_panning_active = is_down;
            break;
        }
        case event_type::mouse_move:
        {
            // 累积鼠标移动量
            ctx.mouse_delta_x += event.mouse_move.xrel;
            ctx.mouse_delta_y += event.mouse_move.yrel; // 注意 SDL y轴方向
            break;
        }
        case event_type::mouse_wheel:
        {
            ctx.scroll_delta_y += event.mouse_wheel.y;
            break;
        }
        case event_type::focus_lost:
        {
            ctx.move_forward = false;
            ctx.move_backward = false;
            ctx.move_left = false;
            ctx.move_right = false;
            ctx.move_up = false;
            ctx.move_down = false;
            ctx.is_free_look_active = false;
            ctx.is_panning_active = false;
            ctx.mouse_delta_x = 0.0F;
            ctx.mouse_delta_y = 0.0F;
            ctx.scroll_delta_y = 0.0F;
            break;
        }
        case event_type::resize:
        {
            // Resize 通常需要更新 Config 的 aspect ratio，这里暂时略过，
            // 实际项目中可以传 CameraTable 进来更新所有相机的 aspect_ratio
            break;
        }
        default:
            break;
        }
    }

    inline void update_camera(camera_container& container, const camera_update_context& ctx, float delta_time)
    {
        for (size_t i = 0; i < container.transforms.size(); ++i)
        {
            auto& transform        = container.transforms[i];
            const auto& config = container.configs[i];

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
                transform.current_zoom = std::max(transform.current_zoom, 1.0F);
                transform.current_zoom = std::min(transform.current_zoom, 45.0F);
                transform.dirty = true;
            }

            // update camera vectors if dirty
            if (transform.dirty)
            {
                glm::vec3 front;
                front.x     = cos(glm::radians(transform.yaw)) * cos(glm::radians(transform.pitch));
                front.y     = sin(glm::radians(transform.pitch));
                front.z     = sin(glm::radians(transform.yaw)) * cos(glm::radians(transform.pitch));
                transform.front = glm::normalize(front);
                transform.right = glm::normalize(glm::cross(transform.front, transform.world_up));
                transform.up    = glm::normalize(glm::cross(transform.right, transform.front));
                transform.dirty = false;
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
} // namespace dod_camera
