#pragma once

#include <cstdint>

namespace interface
{

    enum class key_code : std::uint8_t
    {
        unknown,
        w,
        a,
        s,
        d,
        q,
        e,
        f,
        space,
        lshift,
        lctrl,
        escape,
        up,
        down,
        left,
        right
    };

    enum class mouse_button : std::uint8_t
    {
        unknown,
        left,
        middle,
        right,
    };

    enum class event_type : std::uint8_t
    {
        none, 
        quit,
        resize,
        focus_lost,
        key_down,
        key_up,
        mouse_move,
        mouse_wheel,
        mouse_button_down,
        mouse_button_up
    };

    struct resize_event
    {
        int width;
        int height;
    };

    struct key_event
    {
        key_code key;
    };

    struct mouse_move_event
    {
        float x;
        float y;
        float xrel;
        float yrel;
    };

    struct mouse_wheel_event
    {
        float x;
        float y;
    };

    struct mouse_button_event
    {
        mouse_button button;
        float x;
        float y;
        bool pressed; // true for down, false for up
    };

    // InputEvent
    // - Represents a generic input event used in window implementation and camera implementation.
    // - This structure serves as a unified container for various types of
    //   input events, such as keyboard, mouse, window resize, and quit events.
    // - The specific type of event is determined by the `type` member, which uses
    //   the EventType enumeration.
    // - The union within the structure allows efficient storage of data for
    //   the specific type of event being handled.
    // note: 
    // - Only one member of the union is valid at any given time, depending on the value of the `type` field.
    // details:
    // The fields in the union are as follows:
    // - `resize`: Contains information about a window resize event.
    // - `key`: Contains information about a keyboard event, such as KeyDown or KeyUp.
    // - `mouse_move`: Holds details for a mouse movement event.
    // - `mouse_wheel`: Represents information about mouse wheel scrolling.
    // - `mouse_button`: Contains data for mouse button press or release events.
    struct input_event
    {
        event_type type;

        union
        {
            resize_event resize;
            key_event key;
            mouse_move_event mouse_move;
            mouse_wheel_event mouse_wheel;
            mouse_button_event mouse_button;
        };
    };

} // namespace interface
