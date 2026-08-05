# VulkanSample Render Graph 迁移说明

## 所有权边界

VulkanSample 继续拥有 Vulkan instance/device/surface、swapchain acquire、queue submit、present、pipeline、shader、descriptor 与相机数据。Render Graph 接管帧内 logical resources、views、attachments、pass 录制边界、barrier、transient allocation、帧事务与 submission plan。

帧内不再创建固定 `VkRenderPass`/`VkFramebuffer`，不再手写 `vkCmdPipelineBarrier*`，也不再由 sample 单独管理 transient depth image。graphics pipeline 通过 `VkPipelineRenderingCreateInfo` 声明 attachment formats。

## 当前帧图

`UploadPass` 在 mesh 首次需要上传时读取 staging buffer，并写入 device-local vertex/index buffer。成功提交后 mesh 标记为 uploaded，后续帧不会重复 copy。

`DrawPass` 读取 vertex/index/uniform buffer，将 acquired swapchain image 作为 imported color attachment，把 RG transient depth image作为 depth attachment。Vulkan backend 在 callback 外自动调用 `vkCmdBeginRendering`/`vkCmdEndRendering`，并把最终 swapchain state 转回 present。

## 每帧流程

1. acquire swapchain image，并计算由 extent/format、mesh upload 状态及 acquired image 初始状态组成的 graph cache key。swapchain image 首次使用按 `UNDEFINED/discard` 处理，成功 present 后再次 acquire 才按 `PRESENT/preserve` 处理。
2. `begin_frame(frame_serial, completed_serial, cache_key)`。
3. cache key 变化或无有效计划时 compile；兼容 allocation/view 会选择性复用。
4. 将本帧 acquired image rebind 到 imported image handle。
5. execute 录制抽象计划 lower 后的 Vulkan commands。
6. submit 成功后 `commit_frame()`；失败、out-of-date 或中止时 `abort_frame()`，下一帧 tracker 不继承未提交状态。
7. present。

当前 VulkanSample 的实际 device queue families 相同，因此明确选择单 graphics queue。Core 已支持 graphics/compute/copy batches、timeline waits、release/acquire ownership transfer 与单队列确定性回退；平台层未来可按 `get_submission_plan()` 接入多个 command context 和 submit。

## Resize 与帧间资源

acquired swapchain image 每帧 rebind，不要求 recompile。swapchain extent/format 改变时更新 cache key，仅使相关 transient allocation 和 views 失效。persistent/history resource 可携带跨帧 initial/final state；frames-in-flight 的旧资源按 completed frame 延迟销毁。

## 验证

自动化验收包含：

- Render Graph Debug/Release 全部 CTest。
- Vulkan 与 DX12 backend sample target 编译。
- VulkanSample Debug/Release 编译。
- Upload→Draw 等价图、final present、depth lifetime、无冗余 upload、frame abort/recovery、resize reuse 和多队列计划单元测试。
- 固定 seed 的 96-pass DAG/subresource 压力回归与确定性 dump 对比。
- CLI 参数、相对 asset 路径、内存三角形数据和 swapchain image 状态 tracker 单元测试。

`src/main.cpp` 已启用。`--smoke-test` 使用内存三角形，不依赖 Sponza；未指定 `--frames` 时默认运行 3 个成功呈现帧。smoke path 会检查 UploadPass 恰好执行一次、DrawPass/present 次数等于目标帧数，并在 `--validation` 下把 validation error 计数作为进程失败条件。建议用 6 帧覆盖每个 swapchain image 的首次使用和重复 acquire：

```powershell
.\build\Debug\VulkanSample.exe --smoke-test --frames 6 --validation
```

加载实际模型时可使用 `--config <app_config.json>`，也可用 `--asset <scene.gltf>` 覆盖配置中的 asset 路径。resize、最小化恢复和 Sponza 视觉结果仍建议在目标机器上手动验收。
