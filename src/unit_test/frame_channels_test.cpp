// engine::frame_channels 单元测试
// 覆盖：行表通道发布/查找、状态通道、缺失返回空、帧首清零、多通道并存、
//       每通道单写者（重复发布不覆盖）、id 类型隔离。

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

#include "engine/frame_channels.h"

namespace
{
    int failures = 0;

    void check(bool condition, const char* name)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    // 通道类型（测试局部）：行表 + 状态
    struct camera_rows_t { std::uint32_t dummy = 0; };
    struct instance_rows_t { std::uint32_t dummy = 0; };
    struct lights_table_t { float dummy = 0.0F; };
    struct view_state_t { float dummy = 0.0F; };

    void test_publish_and_find_rows()
    {
        engine::frame_channels channels;
        std::vector<camera_rows_t> cameras{{}, {}, {}};
        channels.publish_rows<camera_rows_t>(cameras);
        const auto found = channels.find_rows<camera_rows_t>();
        check(found.size() == 3, "rows published and found with correct count");
        check(found.data() == cameras.data(), "rows found without copy");
    }

    void test_state_channel()
    {
        engine::frame_channels channels;
        const lights_table_t lights;
        channels.publish_state<lights_table_t>(&lights);
        check(channels.find_state<lights_table_t>() == &lights, "state published and found");
        check(channels.find_state<lights_table_t>() != nullptr, "state pointer non-null");
    }

    void test_missing_channel_returns_empty()
    {
        engine::frame_channels channels;
        check(channels.find_rows<camera_rows_t>().empty(), "missing rows channel returns empty span");
        check(channels.find_state<view_state_t>() == nullptr, "missing state channel returns null");
    }

    void test_clear_resets_frame()
    {
        engine::frame_channels channels;
        std::vector<instance_rows_t> instances{{}, {}};
        channels.publish_rows<instance_rows_t>(instances);
        channels.clear();
        check(channels.find_rows<instance_rows_t>().empty(), "clear empties channel list");
    }

    void test_multiple_channels_coexist()
    {
        engine::frame_channels channels;
        std::vector<camera_rows_t> cameras{{}};
        std::vector<instance_rows_t> instances{{}, {}};
        const lights_table_t lights;
        channels.publish_rows<camera_rows_t>(cameras);
        channels.publish_rows<instance_rows_t>(instances);
        channels.publish_state<lights_table_t>(&lights);
        check(channels.find_rows<camera_rows_t>().size() == 1, "camera channel intact among many");
        check(channels.find_rows<instance_rows_t>().size() == 2, "instance channel intact among many");
        check(channels.find_state<lights_table_t>() == &lights, "state channel intact among many");
    }

    void test_duplicate_publish_is_ignored()
    {
        // 每通道单写者：重复发布不覆盖已发布行（防静默覆盖语义）。
        engine::frame_channels channels;
        std::vector<camera_rows_t> first{{}, {}};
        std::vector<camera_rows_t> second{{}, {}, {}};
        channels.publish_rows<camera_rows_t>(first);
        channels.publish_rows<camera_rows_t>(second);
        const auto found = channels.find_rows<camera_rows_t>();
        check(found.size() == 2 && found.data() == first.data(), "duplicate publish keeps first");
    }

    void test_channel_ids_are_type_isolated()
    {
        check(engine::frame_channels::channel_id<camera_rows_t>() !=
                  engine::frame_channels::channel_id<instance_rows_t>(),
              "distinct types get distinct channel ids");
        check(engine::frame_channels::channel_id<camera_rows_t>() ==
                  engine::frame_channels::channel_id<camera_rows_t>(),
              "same type gets stable channel id");
    }
} // namespace

int main()
{
    test_publish_and_find_rows();
    test_state_channel();
    test_missing_channel_returns_empty();
    test_clear_resets_frame();
    test_multiple_channels_coexist();
    test_duplicate_publish_is_ignored();
    test_channel_ids_are_type_isolated();

    if (failures != 0)
    {
        std::cerr << failures << " frame_channels test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "frame_channels tests passed\n";
    return EXIT_SUCCESS;
}
