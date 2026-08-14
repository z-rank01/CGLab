#pragma once

// engine::frame_channels —— 类型键控帧通道。
//
// 职责边界：
// - 引擎保证秩序：帧首 clear()、发布窗口 = 阶段表顺序（engine 侧发布）、每通道单写者
//   （debug 下重复发布 assert，release 保留首个发布）、数据生存期 = 单帧（通道只存指针，
//   不持有数据）。
// - 编写者保证语义：消费者 find_rows<T>()/find_state<T>() 缺失返回空 span / nullptr，
//   引擎不校验内容——缺通道或数据不对是编写者责任。
//
// 用法：
// - 行表通道（span + count）：publish_rows<T>(span<const T>)，find_rows<T>()。
// - 单例/SoA 表通道：publish_state<T>(const T*)（count=1），find_state<T>()。
// - channel_id<T>() 由静态局部原子计数器生成，全二进制唯一（主仓单可执行成立；
//   DLL 化时需评审——静态局部跨 DLL 不共享）。
//
// 存储：三列 SoA（channel_ids / channel_data / channel_counts，槽位对齐）。查找与
// 去重只扫 id 列（4 字节/槽），数据指针与行数仅在命中时读取；帧首 clear，reserve
// 后稳态零分配。通道数 ~10，线性扫为纳秒级。

#include <atomic>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace engine
{
    struct frame_channels
    {
        std::vector<std::uint32_t> channel_ids;    // 通道类型 id 列
        std::vector<const void*> channel_data;     // 行指针 / 状态指针列（不持有数据）
        std::vector<std::uint32_t> channel_counts; // 行数列（状态通道 = 1）

        void clear() noexcept
        {
            channel_ids.clear();
            channel_data.clear();
            channel_counts.clear();
        }

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
            const std::size_t slot = find_slot(channel_id<T>());
            if (slot == channel_ids.size())
            {
                return {};
            }
            return std::span<const T>(static_cast<const T*>(channel_data[slot]), channel_counts[slot]);
        }

        // 查找状态通道；缺失返回 nullptr。
        template <typename T>
        [[nodiscard]] const T* find_state() const noexcept
        {
            const std::size_t slot = find_slot(channel_id<T>());
            if (slot == channel_ids.size() || channel_counts[slot] == 0)
            {
                return nullptr;
            }
            return static_cast<const T*>(channel_data[slot]);
        }

    private:
        static std::uint32_t next_channel_id() noexcept
        {
            static std::atomic<std::uint32_t> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        void push(std::uint32_t id, const void* data, std::uint32_t count)
        {
            for (const std::uint32_t existing : channel_ids)
            {
                if (existing == id)
                {
                    // 每通道单写者：重复发布是编写者错误。debug 显式 assert 暴露；
                    // release 保留首个发布（通道数据生存期 = 单帧，宁可见不崩溃）。
                    assert(false && "frame channel published twice: single writer per channel");
                    return;
                }
            }
            channel_ids.push_back(id);
            channel_data.push_back(data);
            channel_counts.push_back(count);
        }

        [[nodiscard]] std::size_t find_slot(std::uint32_t id) const noexcept
        {
            for (std::size_t slot = 0; slot < channel_ids.size(); ++slot)
            {
                if (channel_ids[slot] == id)
                {
                    return slot;
                }
            }
            return channel_ids.size();
        }
    };
} // namespace engine
