#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "camera_component.h"
#include "input.h"

// interface::input_router
// - 输入路由层：把"物理输入"（key_code / mouse_button）映射为"逻辑动作"（camera_action），
//   相机系统只消费动作状态，不再硬编码键码。
// - 绑定表是普通数据（vector of pairs），默认绑定由 make_default_bindings() 给出；
//   后续可无缝替换为 JSON 配置驱动（当前为代码表）。
// - 设计约束：camera_system::process_event 保持原有签名与行为（CTest 依赖），
//   它现在只是 process_event_routed + 默认绑定表的薄封装。

namespace interface
{
    enum class camera_action : std::uint8_t
    {
        move_forward,
        move_backward,
        move_left,
        move_right,
        move_up,
        move_down,
        free_look,  // 视角观察（fly：右键）
        orbit_look, // 环绕旋转（orbit：左键）
        pan,        // 平移（中键）
    };

    struct action_binding_table
    {
        std::vector<std::pair<key_code, camera_action>> key_bindings;
        std::vector<std::pair<mouse_button, camera_action>> mouse_bindings;
    };

    // 默认绑定：与重构前的硬编码行为完全一致
    inline action_binding_table make_default_bindings()
    {
        action_binding_table table;
        table.key_bindings = {
            {key_code::w, camera_action::move_forward},
            {key_code::s, camera_action::move_backward},
            {key_code::a, camera_action::move_left},
            {key_code::d, camera_action::move_right},
            {key_code::e, camera_action::move_up},
            {key_code::space, camera_action::move_up},
            {key_code::q, camera_action::move_down},
            {key_code::lctrl, camera_action::move_down},
        };
        table.mouse_bindings = {
            {mouse_button::right, camera_action::free_look},
            {mouse_button::middle, camera_action::pan},
            {mouse_button::left, camera_action::orbit_look},
        };
        return table;
    }

    inline void set_action_state(camera_update_context& ctx, camera_action action, bool active)
    {
        switch (action)
        {
        case camera_action::move_forward:
            ctx.move_forward = active;
            break;
        case camera_action::move_backward:
            ctx.move_backward = active;
            break;
        case camera_action::move_left:
            ctx.move_left = active;
            break;
        case camera_action::move_right:
            ctx.move_right = active;
            break;
        case camera_action::move_up:
            ctx.move_up = active;
            break;
        case camera_action::move_down:
            ctx.move_down = active;
            break;
        case camera_action::free_look:
            ctx.is_free_look_active = active;
            break;
        case camera_action::orbit_look:
            ctx.is_orbit_look_active = active;
            break;
        case camera_action::pan:
            ctx.is_panning_active = active;
            break;
        }
    }

    inline void clear_all_actions(camera_update_context& ctx)
    {
        ctx.move_forward         = false;
        ctx.move_backward        = false;
        ctx.move_left            = false;
        ctx.move_right           = false;
        ctx.move_up              = false;
        ctx.move_down            = false;
        ctx.is_free_look_active  = false;
        ctx.is_panning_active    = false;
        ctx.is_orbit_look_active = false;
        ctx.mouse_delta_x        = 0.0F;
        ctx.mouse_delta_y        = 0.0F;
        ctx.scroll_delta_y       = 0.0F;
    }

    // 路由版事件处理：键码/鼠标键 → 动作状态
    inline void process_event_routed(camera_update_context& ctx,
                                     const input_event& event,
                                     const action_binding_table& bindings)
    {
        switch (event.type)
        {
        case event_type::key_down:
        case event_type::key_up:
        {
            const bool is_down = (event.type == event_type::key_down);
            for (const auto& [key, action] : bindings.key_bindings)
            {
                if (key == event.key.key)
                {
                    set_action_state(ctx, action, is_down);
                }
            }
            break;
        }
        case event_type::mouse_button_down:
        case event_type::mouse_button_up:
        {
            const bool is_down = (event.type == event_type::mouse_button_down);
            for (const auto& [button, action] : bindings.mouse_bindings)
            {
                if (button == event.mouse_button.button)
                {
                    set_action_state(ctx, action, is_down);
                }
            }
            break;
        }
        case event_type::mouse_move:
        {
            ctx.mouse_delta_x += event.mouse_move.xrel;
            ctx.mouse_delta_y += event.mouse_move.yrel; // 注意 SDL y 轴方向
            break;
        }
        case event_type::mouse_wheel:
        {
            ctx.scroll_delta_y += event.mouse_wheel.y;
            break;
        }
        case event_type::focus_lost:
        {
            clear_all_actions(ctx);
            break;
        }
        default:
            break;
        }
    }

} // namespace interface
