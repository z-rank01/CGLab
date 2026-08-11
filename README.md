# CGLab

CGLab 是一个面向 Vulkan 的 C++20 图形框架实验项目。应用共享
`engine_runtime`、asset runtime 和 `cglab_render_graph_vulkan`：

- `TriangleSample`：最小三角形，用于验证窗口、runtime、Vulkan backend 和帧循环。
- `GltfSponzaSample`：支持 `.gltf/.glb`，默认可使用仓库内的 `assets/triangle.gltf`。

## 首次初始化

正式支持 Windows MSVC/Ninja 和 Linux Clang/Ninja。系统工具由用户自行安装，初始化脚本只检测环境、下载项目依赖并配置构建，不执行提权或修改系统安装。

Windows PowerShell：

```powershell
.\scripts\cglab.ps1 setup
```

Linux：

```bash
bash scripts/cglab.sh setup
```

`setup` 会依次检查工具、初始化 HTTPS 子模块、从 `vcpkg.json` 读取固定 baseline、bootstrap vcpkg、安装 manifest 依赖，并配置 Debug preset。重复执行是安全的；如果 vcpkg 工作树存在本地修改，脚本会停止而不会覆盖它。

环境诊断：

```powershell
.\scripts\cglab.ps1 doctor
```

```bash
bash scripts/cglab.sh doctor
```

编译项目所需的 Vulkan headers/loader 由 vcpkg manifest 管理。重新编译 shader 需要 Vulkan SDK 中的 `glslc`；GPU smoke 还需要系统 Vulkan 驱动和可用设备。

## 编译和测试

命令行入口的工具链和配置可以分开选择：

```powershell
.\scripts\cglab.ps1 build --preset windows-msvc-ninja --config Release --target TriangleSample
.\scripts\cglab.ps1 build --preset windows-msvc-ninja --config Release --target GltfSponzaSample
.\scripts\cglab.ps1 test --preset windows-msvc-ninja --config Debug
```

```bash
bash scripts/cglab.sh build --preset linux-clang-ninja --config Release --target TriangleSample
bash scripts/cglab.sh test --preset linux-clang-ninja --config Debug
```

构建目录按工具链和配置隔离，例如 `build/windows-msvc-ninja/Release`。因此切换 Debug/Release 或工具链不会共用一个 CMake cache。

GPU smoke：

```powershell
.\scripts\cglab.ps1 smoke --preset windows-msvc-ninja --config Release --target TriangleSample --frames 6
.\scripts\cglab.ps1 smoke --preset windows-msvc-ninja --config Release --target GltfSponzaSample --asset assets/triangle.gltf --frames 6
```

在 VS Code 中可运行以下 tasks：`Setup workspace`、`Diagnose environment`、`Configure`、`Build selected target`、`Build arbitrary target`、`Test all`、两个 smoke、`Clean current preset`。CMake Tools 和 C/C++ 扩展只是推荐项；未安装扩展时 tasks 和脚本仍可用。

## 自定义工具链和路径

不要修改提交的 `CMakePresets.json`。复制 `CMakeUserPresets.json.example` 为未提交的 `CMakeUserPresets.json`，然后继承仓库 preset，覆盖编译器、build 目录、triplet 或额外 cache variables：

```powershell
Copy-Item CMakeUserPresets.json.example CMakeUserPresets.json
```

常用环境覆盖项包括 `VCPKG_ROOT`、`VULKAN_SDK`、`CGLAB_PRESET`、`CGLAB_CONFIG` 和 `CGLAB_BUILD_DIR`。命令行参数优先级最高，其次是用户 preset，再其次是环境变量和仓库默认值。

正式 preset 为：

- `windows-msvc-ninja-{debug,release,relwithdebinfo}`
- `linux-clang-ninja-{debug,release,relwithdebinfo}`

其他编译器或生成器可以在 `CMakeUserPresets.json` 中继承 `base` preset 自行添加，不会污染仓库配置。

## 可选第三方 targets

默认构建包含两个现代 app。Render Graph、digital-content-loader 的额外 samples 和测试由根 CMake 选项控制，普通用户不需要启用它们。所有 CTest targets 可通过 `test` action 构建并执行。历史 Vulkan 样例仅保存在 `archive/legacy_vulkan/`，不属于构建图。

## 架构文档

当前所有权边界见 [Render Graph 与 Vulkan Backend](docs/RenderGraphAndRHI.md)。整体模块划分见 [Architecture](docs/Architecture.md) 和 [Application Runtime](docs/ApplicationRuntime.md)。

## CGLab

CGLab is a C++20 Vulkan framework experiment. The repository provides portable setup scripts for Windows MSVC/Ninja and Linux Clang/Ninja. See the Chinese quick-start section above for the supported workflow.
