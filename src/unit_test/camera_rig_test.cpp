// camera_rig / input_router 单元测试
// 覆盖：动作映射路由、orbit 模式几何、模式切换视角连续性、bookmark 保存/跳转混合。

#include "_interface/camera_system.h"
#include "_interface/input_router.h"

#include <cmath>
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

    bool nearly_equal(float left, float right, float epsilon = 0.001F)
    {
        return std::abs(left - right) < epsilon;
    }

    bool vec_nearly_equal(const glm::vec3& left, const glm::vec3& right, float epsilon = 0.001F)
    {
        return glm::length(left - right) < epsilon;
    }

    interface::input_event make_key_event(interface::event_type type, interface::key_code key)
    {
        interface::input_event event{};
        event.type     = type;
        event.key.key  = key;
        return event;
    }

    interface::input_event make_button_event(interface::event_type type, interface::mouse_button button)
    {
        interface::input_event event{};
        event.type                = type;
        event.mouse_button.button = button;
        return event;
    }

    interface::input_event make_move_event(float xrel, float yrel)
    {
        interface::input_event event{};
        event.type              = interface::event_type::mouse_move;
        event.mouse_move.xrel   = xrel;
        event.mouse_move.yrel   = yrel;
        return event;
    }

    interface::input_event make_wheel_event(float y)
    {
        interface::input_event event{};
        event.type           = interface::event_type::mouse_wheel;
        event.mouse_wheel.y  = y;
        return event;
    }
} // namespace

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    using namespace interface;

    // --- input_router：默认绑定映射 ---
    {
        const action_binding_table bindings = make_default_bindings();
        camera_update_context ctx;

        process_event_routed(ctx, make_key_event(event_type::key_down, key_code::w), bindings);
        CHECK(ctx.move_forward);
        process_event_routed(ctx, make_key_event(event_type::key_up, key_code::w), bindings);
        CHECK(!ctx.move_forward);

        process_event_routed(ctx, make_button_event(event_type::mouse_button_down, mouse_button::left), bindings);
        CHECK(ctx.is_orbit_look_active);
        CHECK(!ctx.is_free_look_active);

        process_event_routed(ctx, make_button_event(event_type::mouse_button_down, mouse_button::right), bindings);
        CHECK(ctx.is_free_look_active);

        // focus_lost 清空全部动作（含新增的 orbit_look）
        input_event focus{};
        focus.type = event_type::focus_lost;
        process_event_routed(ctx, focus, bindings);
        CHECK(!ctx.is_orbit_look_active);
        CHECK(!ctx.is_free_look_active);
    }

    // --- input_router：自定义绑定生效（动作与键码解耦） ---
    {
        action_binding_table bindings;
        bindings.key_bindings = {{key_code::up, camera_action::move_forward}};
        camera_update_context ctx;
        process_event_routed(ctx, make_key_event(event_type::key_down, key_code::up), bindings);
        CHECK(ctx.move_forward);
        process_event_routed(ctx, make_key_event(event_type::key_up, key_code::up), bindings);
        CHECK(!ctx.move_forward);
        process_event_routed(ctx, make_key_event(event_type::key_down, key_code::w), bindings);
        CHECK(!ctx.move_forward); // w 未绑定，不应触发任何动作
    }

    // --- orbit：环绕保持距离、视线指向焦点 ---
    {
        camera_container cameras;
        const size_t idx = cameras.add_camera();
        camera_update_context ctx;

        set_camera_mode(cameras, idx, camera_mode::orbit);
        camera_transform& transform = cameras.transforms[idx];
        transform.focus_point       = glm::vec3(0.0F);
        transform.orbit_distance    = 10.0F;

        // 左键拖拽旋转
        const std::array events{
            make_button_event(event_type::mouse_button_down, mouse_button::left),
            make_move_event(45.0F, 0.0F),
        };
        tick(cameras, ctx, std::span<const input_event>(events), 0.0F);

        CHECK(nearly_equal(glm::length(transform.position - transform.focus_point), 10.0F, 0.01F));
        // 视线应指向焦点：focus ≈ position + front * distance
        const glm::vec3 aimed = transform.position + transform.front * transform.orbit_distance;
        CHECK(vec_nearly_equal(aimed, transform.focus_point, 0.01F));
        // yaw 确实变了
        CHECK(!nearly_equal(transform.yaw, -90.0F));
    }

    // --- orbit：滚轮 dolly 改变距离并受值域约束 ---
    {
        camera_container cameras;
        const size_t idx = cameras.add_camera();
        camera_update_context ctx;
        set_camera_mode(cameras, idx, camera_mode::orbit);
        camera_transform& transform = cameras.transforms[idx];
        transform.focus_point       = glm::vec3(0.0F);
        transform.orbit_distance    = 10.0F;

        const std::array zoom_in{make_wheel_event(2.0F)};
        tick(cameras, ctx, std::span<const input_event>(zoom_in), 0.0F);
        CHECK(transform.orbit_distance < 10.0F);
        CHECK(nearly_equal(glm::length(transform.position - transform.focus_point), transform.orbit_distance, 0.01F));

        // 持续拉远应被 max_focus_dist 截断
        const std::array zoom_out{make_wheel_event(-10000.0F)};
        tick(cameras, ctx, std::span<const input_event>(zoom_out), 0.0F);
        CHECK(nearly_equal(transform.orbit_distance, cameras.configs[idx].max_focus_dist));
    }

    // --- 模式切换视角连续：fly → orbit 瞬间 front 不变 ---
    {
        camera_container cameras;
        const size_t idx = cameras.add_camera();
        camera_transform& transform = cameras.transforms[idx];
        recompute_camera_basis(transform);

        const glm::vec3 front_before = transform.front;
        set_camera_mode(cameras, idx, camera_mode::orbit);
        // 焦点应位于原视线上
        const glm::vec3 expected_focus = transform.position + front_before * transform.orbit_distance;
        CHECK(vec_nearly_equal(transform.focus_point, expected_focus, 0.01F));

        camera_update_context ctx;
        const std::span<const input_event> no_events{};
        tick(cameras, ctx, no_events, 0.0F);
        // 更新后视线仍通过焦点
        const glm::vec3 aimed = transform.position + transform.front * transform.orbit_distance;
        CHECK(vec_nearly_equal(aimed, transform.focus_point, 0.01F));
    }

    // --- bookmark：保存 → 移动 → 跳转混合到原位 ---
    {
        camera_container cameras;
        const size_t idx = cameras.add_camera();
        camera_update_context ctx;
        const std::span<const input_event> no_events{};

        CHECK(save_bookmark(cameras, idx, 2));
        const camera_pose saved = cameras.bookmarks[idx].poses[2];

        // 移走
        const std::array move{make_key_event(event_type::key_down, key_code::w)};
        tick(cameras, ctx, std::span<const input_event>(move), 1.0F);
        CHECK(!vec_nearly_equal(cameras.transforms[idx].position, saved.position));

        // 跳转：混合完成后应回到保存位姿
        CHECK(goto_bookmark(cameras, idx, 2));
        CHECK(cameras.blends[idx].active);
        // 混合期间输入被屏蔽（W 仍按下但位置只由插值决定）
        tick(cameras, ctx, no_events, 0.3F); // 半途中
        tick(cameras, ctx, no_events, 0.3F); // 完成 0.6s 混合
        CHECK(!cameras.blends[idx].active);
        CHECK(vec_nearly_equal(cameras.transforms[idx].position, saved.position, 0.01F));
        CHECK(nearly_equal(cameras.transforms[idx].yaw, saved.yaw, 0.01F));

        // 空槽跳转失败
        CHECK(!goto_bookmark(cameras, idx, 7));
        // 越界槽失败
        CHECK(!save_bookmark(cameras, idx, bookmark_slot_count));
    }

    // --- fly 默认行为回归：默认模式仍是 fly ---
    {
        camera_container cameras;
        cameras.add_camera();
        CHECK(cameras.transforms.front().mode == camera_mode::fly);
    }

    // --- fly zoom 钳制：远程设置的 FOV（>45）不会被滚轮钳回 45 ---
    {
        camera_container cameras;
        const size_t idx = cameras.add_camera();
        camera_update_context ctx;
        cameras.transforms[idx].current_zoom = 60.0F; // 模拟 camera.set_params 远程设置

        const std::array scroll_up{make_wheel_event(0.5F)};
        tick(cameras, ctx, std::span<const input_event>(scroll_up), 0.0F);
        // 只应按 zoom_speed 变化，并钳到 [min_fov, max_fov]，而不是硬钳回 45
        CHECK(nearly_equal(cameras.transforms[idx].current_zoom, 59.0F));

        const std::array scroll_far{make_wheel_event(-10000.0F)};
        tick(cameras, ctx, std::span<const input_event>(scroll_far), 0.0F);
        CHECK(nearly_equal(cameras.transforms[idx].current_zoom, cameras.configs[idx].max_fov));
    }

    std::cout << "All camera rig tests passed\n";
    return EXIT_SUCCESS;
}
