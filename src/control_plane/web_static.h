#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// control_plane HTTP 静态服务的纯函数部分（I1）。
// 只做字符串/路径运算，不触碰 socket 与文件系统（存在性检查归调用方），便于 CTest 单测。
//
// 安全口径：本地工具只绑 127.0.0.1，但仍拒绝一切路径穿越——
// 不做 percent 解码（含 '%' 一律拒绝），拒绝反斜杠/冒号，逐段排除 ".."，
// 拼接后 lexically_normal 并校验仍在 root 之下。

namespace control_plane
{
    // url_path（如 "/index.html?ver=1"）→ root 内的文件路径；非法/越界返回 nullopt。
    // "/" 或空路径映射到 index.html。
    [[nodiscard]] inline std::optional<std::filesystem::path>
    resolve_web_path(const std::filesystem::path& root, std::string_view url_path)
    {
        const std::size_t query = url_path.find_first_of("?#");
        std::string_view path = url_path.substr(0, query);
        if (path.empty() || path == "/")
        {
            path = "/index.html";
        }
        if (path.front() != '/')
        {
            return std::nullopt;
        }
        path.remove_prefix(1);

        std::filesystem::path relative;
        std::size_t begin = 0;
        while (begin <= path.size())
        {
            const std::size_t sep = path.find('/', begin);
            const std::string_view segment =
                path.substr(begin, sep == std::string_view::npos ? sep : sep - begin);
            if (segment.empty() || segment == "." || segment == ".." ||
                segment.find_first_of("\\%:") != std::string_view::npos)
            {
                return std::nullopt;
            }
            relative /= std::string(segment);
            if (sep == std::string_view::npos)
            {
                break;
            }
            begin = sep + 1;
        }

        const std::filesystem::path base = root.lexically_normal();
        const std::filesystem::path full = (base / relative).lexically_normal();
        // 前缀校验：full 必须以 base 开头（防符号链接外的 lexically 逃逸）
        auto base_it = base.begin();
        auto full_it = full.begin();
        for (; base_it != base.end(); ++base_it, ++full_it)
        {
            if (full_it == full.end() || *full_it != *base_it)
            {
                return std::nullopt;
            }
        }
        return full;
    }

    [[nodiscard]] inline std::string_view content_type_for(const std::filesystem::path& path)
    {
        const std::string ext = path.extension().string();
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".css") return "text/css; charset=utf-8";
        if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
        if (ext == ".json" || ext == ".map") return "application/json; charset=utf-8";
        if (ext == ".png") return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif") return "image/gif";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".ico") return "image/x-icon";
        if (ext == ".wasm") return "application/wasm";
        if (ext == ".woff2") return "font/woff2";
        if (ext == ".txt") return "text/plain; charset=utf-8";
        return "application/octet-stream";
    }

} // namespace control_plane
