#include "_interface/camera_system.h"
#include "_interface/sdl_window.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

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

    bool nearly_equal(float left, float right)
    {
        return std::abs(left - right) < 0.0001F;
    }

    interface::input_event make_key_event(interface::event_type type, interface::key_code key)
    {
        interface::input_event event{};
        event.type = type;
        event.key.key = key;
        return event;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    using namespace interface;

    {
        CHECK(SDL_Init(SDL_INIT_EVENTS));
        sdl_window window;
        SDL_Event key_down{};
        key_down.type = SDL_EVENT_KEY_DOWN;
        key_down.key.key = SDLK_W;
        SDL_Event key_up{};
        key_up.type = SDL_EVENT_KEY_UP;
        key_up.key.key = SDLK_W;
        CHECK(SDL_PushEvent(&key_down));
        CHECK(SDL_PushEvent(&key_up));

        std::vector<input_event> events;
        window.poll_events(events);
        CHECK(events.size() == 2);
        CHECK(events[0].type == event_type::key_down);
        CHECK(events[0].key.key == key_code::w);
        CHECK(events[1].type == event_type::key_up);
        CHECK(events[1].key.key == key_code::w);
    }

    {
        camera_container cameras;
        cameras.add_camera();
        camera_update_context context;
        const std::array events{
            make_key_event(event_type::key_down, key_code::w),
            make_key_event(event_type::key_up, key_code::w),
        };
        const glm::vec3 initial_position = cameras.transforms.front().position;
        tick(cameras, context, std::span<const input_event>(events), 1.0F);
        CHECK(!context.move_forward);
        CHECK(cameras.transforms.front().position == initial_position);
    }

    {
        camera_container cameras;
        cameras.add_camera();
        camera_update_context context;
        const std::array down{make_key_event(event_type::key_down, key_code::w)};
        tick(cameras, context, std::span<const input_event>(down), 0.5F);
        const float after_down = cameras.transforms.front().position.z;
        tick(cameras, context, std::span<const input_event>{}, 0.5F);
        CHECK(cameras.transforms.front().position.z < after_down);
        const std::array up{make_key_event(event_type::key_up, key_code::w)};
        tick(cameras, context, std::span<const input_event>(up), 0.5F);
        const float after_up = cameras.transforms.front().position.z;
        tick(cameras, context, std::span<const input_event>{}, 0.5F);
        CHECK(nearly_equal(cameras.transforms.front().position.z, after_up));
    }

    {
        camera_container cameras;
        cameras.add_camera();
        camera_update_context context;
        input_event right_down{};
        right_down.type = event_type::mouse_button_down;
        right_down.mouse_button.button = mouse_button::right;
        input_event motion0{};
        motion0.type = event_type::mouse_move;
        motion0.mouse_move.xrel = 2.0F;
        motion0.mouse_move.yrel = 1.0F;
        input_event motion1{};
        motion1.type = event_type::mouse_move;
        motion1.mouse_move.xrel = 3.0F;
        motion1.mouse_move.yrel = 4.0F;
        const std::array events{right_down, motion0, motion1};
        tick(cameras, context, std::span<const input_event>(events), 0.0F);
        CHECK(nearly_equal(context.mouse_delta_x, 5.0F));
        CHECK(nearly_equal(context.mouse_delta_y, 5.0F));
        CHECK(nearly_equal(cameras.transforms.front().yaw, -89.5F));
        CHECK(nearly_equal(cameras.transforms.front().pitch, -0.5F));

        const std::array focus_lost{
            make_key_event(event_type::key_down, key_code::w),
            input_event{.type = event_type::focus_lost},
        };
        tick(cameras, context, std::span<const input_event>(focus_lost), 0.0F);
        CHECK(!context.is_free_look_active);
        CHECK(!context.is_panning_active);
        CHECK(!context.move_forward);
        CHECK(!context.move_backward);
        CHECK(!context.move_left);
        CHECK(!context.move_right);
        CHECK(nearly_equal(context.mouse_delta_x, 0.0F));
        CHECK(nearly_equal(context.mouse_delta_y, 0.0F));
    }

    {
        camera_container cameras;
        cameras.add_camera();
        camera_update_context context;
        const glm::vec3 initial_position = cameras.transforms.front().position;
        const std::array events{
            make_key_event(event_type::key_down, key_code::w),
            make_key_event(event_type::key_down, key_code::d),
        };
        tick(cameras, context, std::span<const input_event>(events), 1.0F);
        CHECK(nearly_equal(glm::length(cameras.transforms.front().position - initial_position), 10.0F));
    }

    return EXIT_SUCCESS;
}
