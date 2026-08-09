# Vulkan Functional Framework
面向不同图形 API、以函数式编程（Functional Programming）风格构建的个人定制 Vulkan 框架

# Usage（使用）
## 配置 CMakePresets.json
为了编译源码，需要修改 CMakePresets.json 中的多处路径。
1. 选择一个构建链（build chain），例如名称为 `windows-msvc` 或 `linux-clang`
2. 在 `cacheVariables` 部分修改编译器路径
    - `CMAKE_C_COMPILER`
    - `CMAKE_CXX_COMPILER`
    - `CMAKE_RC_COMPILER`
    - ...

## 配置 app_config.json

## Clangd 相关
如果需要使用 clangd 作为（此处补充说明：例如语言服务器/代码补全等）

## Vulkan SDK
要使用该框架，需要下载并安装 Vulkan SDK，并将其加入系统环境变量。

## VulkanSample Render Graph

`feature/vulkan_sample_dod` 已将 VulkanSample 的帧内 upload/draw、资源状态、transient depth、Dynamic Rendering 和同步迁移到 Render Graph。平台层仍负责 acquire、submit 和 present。实现边界、帧事务及验证方式见 [VulkanSample Render Graph 迁移说明](docs/VulkanSampleRenderGraphMigration.md)。

启用 `BUILD_VULKAN_SAMPLE` 或 `RENDER_GRAPH_BUILD_UNIT_TESTS` 后，根 CMake 会接入 `third_party/render-graph`。可使用 CTest 运行 Render Graph 的全部回归测试。

主程序支持 `--config`、`--asset`、`--frames`、`--validation` 和 `--smoke-test`。无需外部模型的串行 GPU 验证可直接运行：

```powershell
.\build\Debug\VulkanSample.exe --smoke-test --frames 6 --validation
```

旧版手写实现保存在 `src/legacy_vulkan_sample`，仅在启用 `CGLAB_BUILD_LEGACY_VULKAN_SAMPLE` 时生成 `VulkanSampleLegacy` target。

## Runtime 与 Samples

当前构建拆分为 `cglab_engine_core`、`cglab_platform_sdl`、`cglab_asset_gltf`、`cglab_framework_runtime`、`cglab_vulkan_renderer` 和 `cglab_application_runner`。framework/engine 公共头不暴露 Vulkan、SDL 或 glTF 类型；应用通过 `engine::render_backend`、`render_snapshot` 和 opaque `geometry_handle` 与 renderer 交互。共享 runner 统一 CLI、退出码、validation 与 smoke counter 检查。

默认生成两个应用：

- `VulkanSample`：支持配置/资产加载、控制平面与 smoke contract 的主示例。
- `TriangleSample`：最小内存三角形，用于验证第二个应用复用同一 runtime。

```powershell
cmake -S . -B build -DCGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
./build/TriangleSample.exe --smoke-test --frames 6 --no-ui
```

依赖清单使用 vcpkg 已验证的 `tinygltf 3.0.0`。旧的 2.9.x registry 源包哈希已无法通过官方校验，因此没有采用修改哈希或跳过校验的方式继续使用它。GPU validation smoke 需要系统安装 `VK_LAYER_KHRONOS_validation`。

---

# Vulkan Functional Framework
Personal customized vulkan framework targeting on different gfx APIs in a style of **Functional Progamming**

# Usage
## Config CMakePresets.json
In order to build source files, one need to modify multiple path inside CMakePresets.json. 
1. Target one of the build chain, like name `windows-msvc` or `linux-clang`
2. Modify the path to the compiler in the part of `cacheVariables`
    - `CMAKE_C_COMPILER`
    - `CMAKE_CXX_COMPILER`
    - `CMAKE_RC_COMPILER`
    - ...

## Config app_config.json

## Clangd Specified
If one want to use clangd as 

## Vulkan SDK
To use this framework, one need to download and install vulkan sdk with system variables added.
