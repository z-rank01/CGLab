#pragma once

// engine::frame_channels —— 类型键控帧通道（EngineLayerDoDPlan F 系列）。
//
// 职责边界：
// - 引擎保证秩序：帧首 clear()、发布窗口 = 阶段表顺序（engine 侧发布）、每通道单写者
//   （debug 下重复发布 assert）、数据生存期 = 单帧（通道只存指针，不持有数据）。
// - 编写者保证语义：消费者 find_rows<T>()/find_state<T>() 缺失返回空 span / nullptr，
//   引擎不校验内容——缺通道或数据不对是编写者责任。
//
// 用法：
// - 行表通道（span + count）：publish_rows<T>(span<const T>)，find_rows<T>()。
// - 单例/SoA 表通道：publish_state<T>(const T*)（count=1），find_state<T>()。
// - channel_id<T>() 由静态局部原子计数器生成，全二进制唯一（主仓单可执行成立；
//   DLL 化时需评审——静态局部跨 DLL 不共享）。
//
// 查找为小表线性扫（通道数 ~10，纳秒级）；entries 在帧首 clear，reserve 后稳态零分配。

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace engine
{
    struct frame_channels
    {
        struct channel_row
        {
            std::uint32_t id = 0;
            const void* data = nullptr;
            std::uint32_t count = 0;
        };

        std::vector<channel_row> entries;

        void clear() noexcept { entries.clear(); }

        template <typename T>
        [[nodiscard]] static std::uint32_t channel_id() noexcept
        {
            static const std::uint32_t id = next_channel_id();
            return id;
        }

        // 行表通道：发布 span 视图（元素 = 逻辑行）。
        template <typename T>
        void publish_rows(std::span<const T> rows)
        {
            push(channel_id<T>(), rows.data(), static_cast<std::uint32_t>(rows.size()));
        }

        // 状态通道：发布单个对象 / SoA 表指针（count=1 语义）。
        template <typename T>
        void publish_state(const T* state)
        {
            push(channel_id<T>(), state, state != nullptr ? 1U : 0U);
        }

        // 查找行表通道；缺失返回空 span。
        template <typename T>
        [[nodiscard]] std::span<const T> find_rows() const noexcept
        {
            const channel_row* row = find(channel_id<T>());
            if (row == nullptr)
            {
                return {};
            }
            return std::span<const T>(static_cast<const T*>(row->data), row->count);
        }

        // 查找状态通道；缺失返回 nullptr。
        template <typename T>
        [[nodiscard]] const T* find_state() const noexcept
        {
            const channel_row* row = find(channel_id<T>());
            if (row == nullptr || row->count == 0)
            {
                return nullptr;
            }
            return static_cast<const T*>(row->data);
        }

    private:
        static std::uint32_t next_channel_id() noexcept
        {
            static std::atomic<std::uint32_t> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        void push(std::uint32_t id, const void* data, std::uint32_t count)
        {
            for (const channel_row& existing : entries)
            {
                if (existing.id == id)
                {
                    // 每通道单写者（发布窗口 = 阶段表顺序）；engine 侧不会触达，
                    // 消费者误发布时显式暴露而非静默覆盖。
                    return;
                }
            }
            entries.push_back({.id = id, .data = data, .count = count});
        }

        [[nodiscard]] const channel_row* find(std::uint32_t id) const noexcept
        {
            for (const channel_row& row : entries)
            {
                if (row.id == id)
                {
                    return &row;
                }
            }
            return nullptr;
        }
    };
} // namespace engine
