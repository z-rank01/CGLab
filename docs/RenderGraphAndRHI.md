# Render Graph 与 Vulkan Backend

> as-built，2026-08。

## 边界

| 层 | 输入 | 所有权 |
|---|---|---|
| Engine runtime | 事件、控制命令、资产完成行 | scene/camera 状态表、phase systems、frame extraction |
| `render_driver` | API 无关资源变更和 SoA frame packet | opaque 调用契约，不拥有 GPU API |
| `src/render_graph_vulkan` | Engine 行表、SDL surface provider | 内容 lowering 与 RG recipe/执行适配 |
| RG Vulkan runtime | 公共 resource desc、upload/draw rows | Vulkan context、资源、descriptor、pipeline、命令、同步与提交 |

Engine 不读取 Vk handle，RG backend 不读取可变 scene/camera 对象。SDL adapter 仅提供 instance extensions、surface 创建和 drawable extent。

## 每帧数据流

```text
asset completion/request rows
        | apply_resource_changes
        v
RG persistent resource/upload tables

scene + camera tables
        | extract_render_packet
        v
instance/transform/material/mesh SoA
        | content lowering
        v
GPU scene tables + indirect groups
        | RG compile/cache + Vulkan execute
        v
acquire -> upload/barrier -> dynamic rendering -> submit -> present -> retire
```

`resource_change_batch` 把 geometry/material/image/sampler 的创建、更新和回收集中到帧边界。兼容便捷函数仍可构造单行 batch，但不会绕过该入口。

## 资源与内存

RG Core 使用 `buffer_desc`、`image_desc`、`memory_domain`、`mapping_policy` 和 `resource_lifetime`。graph recipe 和 Engine 不接受 Vk create info。Vulkan lowering 对不支持的 format/usage/memory 组合返回结构化诊断；DX12/Metal fake lowering tests 固化相同公共语义。

Vulkan allocator 使用 VMA，但 VMA implementation 和所有 create/destroy side effect 均在 RG 子模块。资源策略是 arena + slice：

- upload/readback arena 持久映射，按 range flush/invalidate；
- vertex/index 数据进入可增长的 device-local 大 buffer，并以 offset/range 引用；
- staging slice 在对应 submission 完成后进入 free-span；
- persistent resource 不因 resize 重建；只有尺寸相关 image/view 失效；
- 上传请求数量是稳定 UploadPass 的数据，不进入 graph cache key。

这保留并正规化了早期 VRA 的目标：减少小 buffer 和碎片，通过 offset 复用大分配；区别是分配、同步、生命周期和回收现在由 RG runtime 的行表统一管理。

## Bindless 与 pipeline

逻辑 ABI 固定为五张表：`sampled_images[]`、`samplers[]`、`storage_images[]`、`uniform_buffers[]`、`storage_buffers[]`。slot 0 始终有效；handle 是 32-bit index + generation；retired slot 必须等待 completed submission 才可重用。

Vulkan 使用一个持久 update-after-bind descriptor set。稳定场景每帧 descriptor allocation/update 为零，新资源只更新其新 slot。缺少 descriptor indexing 所需 feature 时，初始化错误会列出具体缺失项。

Pipeline row 的 key 包含 shader stages、vertex layout、raster state 和 Dynamic Rendering attachment compatibility。transform/material/frame 数据位于 GPU tables，不使用 dynamic uniform descriptor offset。

## GPU scene 与 glTF

CPU extraction 输出 mesh instance、transform、material 和 draw packet SoA。backend 生成 GPU draw table 与 indexed indirect command buffer，并按 alpha/cull/pipeline key 分组。相同 mesh 的多个 node 共享 geometry slice；transform 或 material 更新不会重新上传 vertex/index。

glTF Core 2.0 静态 PBR 支持 base color、metallic-roughness、normal、occlusion、emissive、alpha mode/cutoff 和 double-sided。required extension 不支持时明确失败；未知 optional extension 只记录 warning。第一版使用 Engine 方向光，不含 IBL。

## 事务和失效

- acquire 失败或最小化可返回 skipped，不提交半帧；
- graph commit 只发生在成功提交后，abort 保持已发布状态；
- swapchain resize 只使尺寸/格式相关状态失效；
- staging、resource、descriptor slot 与 pipeline retirement 都由 submission 序号控制；
- graph 只在 recipe、尺寸、格式或兼容性变化时重编译。

架构扫描禁止 active `src/` 出现 GPU allocation、descriptor、pipeline、command、submit/present side effect；这些调用必须位于 RG Vulkan backend。
