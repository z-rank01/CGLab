# CGLab Engine 与 Render Graph 架构

> as-built，2026-08。本文描述当前实现，不是迁移草图。

## 依赖与所有权

```text
TriangleSample / GltfSponzaSample
        |-- cglab_application_runner
        |       `-- cglab_engine_runtime
        |              |-- cglab_engine_core
        |              |-- cglab_asset_runtime --> cglab_asset_gltf
        |              `-- cglab_platform_sdl
        |-- Sample-specific API-neutral recipe
        `-- cglab_sdl_vulkan_surface
                `-- render_graph::vulkan
```

Engine 公共头只表达资产、场景、帧数据和渲染驱动契约，不包含 Vulkan、SDL、glTF 或 Render Graph 类型。`src/platform/vulkan/` 只保存 SDL surface provider 与 Engine↔RG function-table bridge；Vulkan instance/device、swapchain、VMA 分配、bindless descriptor、pipeline cache、graph 执行和命令录制全部由 `third_party/render-graph/src/backend/vulkan/` 拥有。

`src/renderer/` 已删除。旧 VRA 和 Vulkan helper 已退出构建，历史样例只保存在 `archive/legacy_vulkan/`。

## Engine 数据模型

Engine 使用 opaque state + function table 的 `render_driver`，不使用 renderer 继承体系。资源变更在帧边界以 `resource_change_batch` 行表提交；`render_frame_packet` 由 camera、instance、transform、material handle、mesh handle 等 SoA view 组成。

runtime 的固定 phase table 为：

```text
poll_events
consume_control_commands
merge_asset_results
update_scene_transforms
update_cameras
run_sample_systems
extract_render_packet
apply_resource_changes
submit_render_packet
publish_telemetry
```

phase 顺序显式固定。worker 只写私有值类型结果，主线程在帧边界合并；load、unload、resize、pause/step 都转换为 request rows。关闭流程可重复执行。

glTF adapter 输出 CPU Asset Database：node/parent/local transform、mesh、primitive、material、image、sampler 行和共享 vertex/index blobs。节点变换不烘焙进顶点，同一 mesh 可由多个 node 实例化。当前支持 Core 2.0 静态 metallic-roughness PBR；动画、蒙皮、morph、KHR 材质扩展和 IBL 不在当前范围。

## Render Graph 与 Vulkan

RG Core 的 render device、buffer/image、pipeline、resource change、frame recipe 和 command 描述与 API 无关；Vulkan lowering 负责选择 Vk usage、memory flags 和格式兼容性。DX12/Metal 当前只有 lowering contract 和 fake tests。

Vulkan runtime 以集中表保存 device、queue、frame、swapchain image、resource、allocation、bindless slot、pipeline 和 retirement 状态，并显式执行：

```text
acquire -> realize_resources -> record_batches -> submit -> present -> collect_retired
```

持久几何、材质、纹理和 frame tables 由 RG 创建。upload arena 采用大 buffer 子分配和 free-span 复用；vertex/index 也使用大 device-local arena 的 slice，而非每个 primitive 一个 VkBuffer。staging slice 和 bindless slot 都在 completed submission gate 后复用。

固定 bindless ABI 包含 sampled images、samplers、storage images、uniform buffers 和 storage buffers。slot 0 是默认资源；CPU handle 使用 index + generation。Vulkan 要求 runtime descriptor array、partially bound、update-after-bind 和 non-uniform indexing，不提供传统 descriptor fallback。

Dynamic Rendering 保留，attachment format 进入 pipeline key。Triangle 与 glTF 分别拥有自己的 recipe；glTF recipe 按 opaque/mask、single/double-sided、blend 分组，透明行按相机距离排序，以 indexed indirect 批量录制。RG backend 不知道 shader 路径或 glTF/PBR 语义。稳定场景不会逐帧分配 descriptor，也不会因上传行数变化重新编译 graph。

## 设计约束

- 数据库行和 SoA 优先于对象继承与散落状态；真正的闭集 discriminant 才使用 enum。
- manager/system 整体遍历表，副作用集中在 phase 末端或 RG Vulkan lowering/execute 边界。
- Engine、资产和 graph recipe 保持 API 无关；特殊能力由 backend capabilities 和结构化诊断表达。
- 新 GPU 资源、descriptor、pipeline 或 command side effect 只能进入 RG Vulkan backend。
- 构建时的 `ArchitectureContract.cmake` 固化这些依赖和调用边界。

更多执行细节见 [RenderGraphAndRHI.md](RenderGraphAndRHI.md)，runtime 组合方式见 [ApplicationRuntime.md](ApplicationRuntime.md)。
