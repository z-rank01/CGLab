# GltfSponzaSample Render Graph 迁移说明

## 所有权边界

`cglab_vulkan_backend` 拥有 Vulkan instance/device/surface、swapchain acquire、queue submit、present、pipeline、shader、descriptor 与 geometry arena。`cglab_engine_runtime` 拥有窗口、camera、scene、控制平面和 asset service，并通过 Vulkan-free `render_snapshot` 提交只读 camera 矩阵和 geometry handles。Render Graph 接管帧内 logical resources、views、attachments、pass 录制边界、barrier、transient allocation、帧事务与 submission plan。

帧内不再创建固定 `VkRenderPass`/`VkFramebuffer`，不再手写 `vkCmdPipelineBarrier*`，也不再由 sample 单独管理 transient depth image。graphics pipeline 通过 `VkPipelineRenderingCreateInfo` 声明 attachment formats。

## 当前帧图

启动资产与运行时资产统一进入 geometry arena。`RuntimeUploadPass` 在 geometry 首次需要上传时读取 staging buffer，并写入 device-local vertex/index arena；成功提交后不再重复 copy。scene/runtime 只保留 opaque geometry handle，arena span 与延迟回收由 renderer 私有管理。

应用通过窄 `engine::vulkan::render_program` 选择 pass 名称和基础 raster 配置；当前 GltfSponzaSample 使用 `DrawPass`，TriangleSample 使用 `TrianglePass`。pass 读取 arena/uniform buffer，将 acquired swapchain image 作为 imported color attachment，把 RG transient depth image 作为 depth attachment。

## 每帧流程

1. acquire swapchain image，并计算由 extent/format、mesh upload 状态及 acquired image 初始状态组成的 graph cache key。swapchain image 首次使用按 `UNDEFINED/discard` 处理，成功 present 后再次 acquire 才按 `PRESENT/preserve` 处理。
2. `begin_frame(frame_serial, completed_serial, cache_key)`。
3. cache key 变化或无有效计划时 compile；兼容 allocation/view 会选择性复用。
4. 将本帧 acquired image rebind 到 imported image handle。
5. execute 录制抽象计划 lower 后的 Vulkan commands。
6. submit 成功后 `commit_frame()`；失败、out-of-date 或中止时 `abort_frame()`，下一帧 tracker 不继承未提交状态。
7. present。

当前 GltfSponzaSample 的实际 device queue families 相同，因此明确选择单 graphics queue。Core 已支持 graphics/compute/copy batches、timeline waits、release/acquire ownership transfer 与单队列确定性回退；平台层未来可按 `get_submission_plan()` 接入多个 command context 和 submit。

## Resize 与帧间资源

acquired swapchain image 每帧 rebind，不要求 recompile。swapchain extent/format 改变时更新 cache key，仅使相关 transient allocation 和 views 失效。persistent/history resource 可携带跨帧 initial/final state；frames-in-flight 的旧资源按 completed frame 延迟销毁。

## 验证

自动化验收包含：

- Render Graph Debug/Release 全部 CTest。
- GltfSponzaSample、TriangleSample 与 Render Graph targets 的 Debug/Release 编译。
- opt-in `VulkanSampleLegacy` target 编译。
- Upload→Draw 等价图、final present、depth lifetime、无冗余 upload、frame abort/recovery、resize reuse 和多队列计划单元测试。
- 固定 seed 的 96-pass DAG/subresource 压力回归与确定性 dump 对比。
- CLI 参数、相对 asset 路径、内存三角形数据和 swapchain image 状态 tracker 单元测试。

`GltfSponzaSample` 已启用。`--smoke-test` 在未指定 `--asset` 时加载仓库内的 `assets/triangle.gltf`，不依赖 Sponza；未指定 `--frames` 时默认运行 3 个成功呈现帧。smoke path 会检查 UploadPass 恰好执行一次、DrawPass/present 次数等于目标帧数，并在 `--validation` 下把 validation error 计数作为进程失败条件。建议用 6 帧覆盖每个 swapchain image 的首次使用和重复 acquire：

```powershell
.\build\Debug\GltfSponzaSample.exe --smoke-test --frames 6 --validation
.\build\Debug\TriangleSample.exe --smoke-test --frames 6 --validation
```

运行 `--validation` 前必须安装 `VK_LAYER_KHRONOS_validation`；缺失时 renderer 初始化会返回 `ErrorLayerNotPresent` 并以失败退出，不会静默降级。

加载实际模型时使用 `--asset <scene.gltf|scene.glb>`。resize、最小化恢复和 Sponza 视觉结果仍建议在目标机器上手动验收。
