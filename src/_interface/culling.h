#pragma once

// interface::culling —— 视锥剔除纯函数层。
// 零引擎类型依赖（仅 glm/std），供 engine 剔除系统与单测消费；
// 所有函数无副作用，世界空间约定与渲染一致。

#include <array>

#include <glm/glm.hpp>

namespace interface::culling
{
    // 世界空间 6 平面视锥。平面 = {normal.xyz, d}，法线向外；
    // 点在视锥内 ⟺ 对所有平面 dot(plane, p) <= 0（Gribb-Hartmann 提取，已归一化）。
    struct frustum
    {
        std::array<glm::vec4, 6> planes{};
    };

    // 从 view_projection 提取 6 个外向平面（GLM 约定：view_projection = projection * view）。
    [[nodiscard]] inline frustum make_frustum(const glm::mat4& view_projection)
    {
        // GLM 列主序：m[col][row]；先取行向量再组合平面。
        const glm::vec4 row0{view_projection[0][0], view_projection[1][0], view_projection[2][0], view_projection[3][0]};
        const glm::vec4 row1{view_projection[0][1], view_projection[1][1], view_projection[2][1], view_projection[3][1]};
        const glm::vec4 row2{view_projection[0][2], view_projection[1][2], view_projection[2][2], view_projection[3][2]};
        const glm::vec4 row3{view_projection[0][3], view_projection[1][3], view_projection[2][3], view_projection[3][3]};

        // 裁剪空间内部：-w <= x <= w、-w <= y <= w、-w <= z <= w（GLM 透视，OpenGL Z 约定）。
        // 外向法线 = 取反后归一化：外向侧 dot(plane, p) > 0。
        const auto make_plane = [](const glm::vec4& inward)
        {
            const glm::vec4 outward = -inward;
            const float length = glm::length(glm::vec3(outward));
            return outward / length;
        };
        frustum result;
        result.planes[0] = make_plane(row3 + row0); // left:   x >= -w
        result.planes[1] = make_plane(row3 - row0); // right:  x <=  w
        result.planes[2] = make_plane(row3 + row1); // bottom: y >= -w
        result.planes[3] = make_plane(row3 - row1); // top:    y <=  w
        result.planes[4] = make_plane(row3 + row2); // near:   z >= -w
        result.planes[5] = make_plane(row3 - row2); // far:    z <=  w
        return result;
    }

    // AABB 与视锥的保守相交测试：对每个外向平面取"最内侧角点"（外向法线下 dot 最小的角点），
    // 若其仍在外侧（dot > 0）则整体在外，剔除；相切（dot == 0）判可见（保守，不丢面）。
    [[nodiscard]] inline bool test_aabb(const frustum& frustum, const glm::vec3& min, const glm::vec3& max)
    {
        for (const glm::vec4& plane : frustum.planes)
        {
            const glm::vec3 corner{
                plane.x < 0.0F ? max.x : min.x,
                plane.y < 0.0F ? max.y : min.y,
                plane.z < 0.0F ? max.z : min.z,
            };
            if (glm::dot(glm::vec3(plane), corner) + plane.w > 0.0F)
            {
                return false;
            }
        }
        return true;
    }
} // namespace interface::culling
