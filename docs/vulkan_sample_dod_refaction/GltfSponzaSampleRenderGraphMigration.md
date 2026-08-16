# GltfSponzaSample Render Graph 迁移记录

该迁移已完成；本文仅保留结果摘要。当前权威架构见 [Architecture.md](Architecture.md) 与 [RenderGraphAndRHI.md](RenderGraphAndRHI.md)。

- glTF adapter 输出保留 node hierarchy、mesh instancing、material/image/sampler 的 CPU Asset Database。
- Engine 通过 `resource_change_batch` 和 SoA `render_frame_packet` 与渲染层交互。
- `src/renderer/`、旧 VRA 和 Vulkan helpers 已退出 active source tree。
- RG Vulkan runtime 拥有资源分配、bindless descriptor、pipeline、Dynamic Rendering、indexed indirect 命令和 submission 生命周期。
- TriangleSample 与 GltfSponzaSample 共享同一 runtime/driver 路径，并以 6-frame smoke 作为迁移验证。
