# 从 clone 到编译

本文说明在一台新电脑上获取 CGLab 后，如何初始化依赖、选择工具链和构建 app。仓库脚本是唯一推荐的执行入口；VS Code tasks 只是对同一组脚本的快捷封装。

## 1. 支持的组合

仓库提供两组正式的 CMake configure preset，每组支持三种构建类型：

| 平台 | 工具链 | 配置目录示例 |
| --- | --- | --- |
| Windows | Visual Studio 2022 C++ + Ninja | `build/windows-msvc-ninja/Debug` |
| Linux | Clang + Ninja | `build/linux-clang-ninja/Release` |

可用配置是 `Debug`、`Release` 和 `RelWithDebInfo`。不同工具链和配置使用独立的 build tree，因此不要在同一个 build 目录之间切换编译器。

项目编译所需的 Vulkan headers、loader 和其他第三方库由 vcpkg manifest 管理。重新编译 shader 需要 Vulkan SDK 中的 `glslc`；运行 GPU smoke 需要系统中可用的 Vulkan 驱动和设备。这三项能力彼此独立：没有 Vulkan SDK 不一定阻止使用仓库中已有的 SPIR-V 编译，不能运行 GPU smoke 也不影响 C++ 单元测试。

## 2. 安装宿主工具

脚本只检查工具并给出建议，不会自动安装系统软件或请求管理员权限。

Windows 至少需要：

- Git；
- CMake；
- Ninja；
- Visual Studio 2022 的 Desktop development with C++ workload。

脚本通过 `vswhere` 找到可用的 Visual Studio 2022，并自动加载 x64 MSVC 环境，不依赖 Community edition、固定 Windows SDK 或固定 MSVC 小版本。Ninja 如果没有加入 `PATH`，脚本也会检查常见的 `C:\Program Files\Ninja` 目录。Windows 上调用 CMake 时，脚本会临时把当前控制台代码页切换为 UTF-8，并在命令结束后恢复原值，确保 CMake/Ninja 能稳定解析本地化的 MSVC `/showIncludes` 输出并追踪头文件依赖。

Linux 至少需要：

- Git、CMake、`ninja-build`；
- `clang` 和 `clang++`；
- 能提供 Vulkan loader 和驱动的系统 Vulkan 运行环境。

使用新版本 CMake 时可以使用 `--fresh`。如果要使用 `Configure fresh`，建议 CMake 3.24 或更高版本。

## 3. Clone 仓库

```text
git clone <repository-url> CGLab-vulkan-sample-dod
cd CGLab-vulkan-sample-dod
```

如果仓库已经 clone，只需要进入仓库根目录。不要手动复制 vcpkg 或子模块；初始化脚本会使用 HTTPS 同步两个项目子模块，并按照 `vcpkg.json` 中的 `builtin-baseline` 固定 vcpkg 版本。

## 4. 首次 Setup

### Windows PowerShell

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\cglab.ps1 setup
```

默认使用 `windows-msvc-ninja-debug`，完成后可以直接构建 Debug app。也可以在第一次 setup 时选择其他配置：

```powershell
.\scripts\cglab.ps1 setup --preset windows-msvc-ninja --config Release
```

### Linux Bash

```bash
bash scripts/cglab.sh setup
```

默认使用 `linux-clang-ninja-debug`。

`setup` 的顺序是：

1. 检查 Git、CMake、Ninja、编译器和可选的 Vulkan 运行环境；
2. 执行 `git submodule sync --recursive` 和 `git submodule update --init --recursive`；
3. 读取 `vcpkg.json` 的 baseline；
4. 在默认的 `vcpkg/` 目录 clone 官方 vcpkg，切换到该 baseline 并 bootstrap；
5. 使用当前平台 triplet 安装 manifest 依赖；
6. 对所选 preset 执行一次 fresh configure。

`vcpkg/`、`vcpkg_installed/`、baseline bootstrap 标记文件和 build tree 都是本地生成内容，不应提交。重复执行 setup 是安全的：已有正确 remote 且工作树干净的 vcpkg checkout 会被复用，已存在的 baseline、bootstrap 和依赖缓存也会尽量跳过重复工作。

如果已有的 vcpkg remote 不正确，或者 vcpkg 工作树有本地修改，脚本会停止并保留用户内容。请先处理该 checkout，之后再重新执行 setup；脚本不会自动覆盖或删除它。

初始化前只检查环境时运行：

```powershell
.\scripts\cglab.ps1 doctor
```

```bash
bash scripts/cglab.sh doctor
```

## 5. 选择配置并编译一个 app

脚本可以接受工具链基名，也可以接受完整的 configure preset。下面的命令会自动把 `--config Release` 解析成 `windows-msvc-ninja-release`：

### Windows

```powershell
.\scripts\cglab.ps1 configure --preset windows-msvc-ninja --config Release
.\scripts\cglab.ps1 build --preset windows-msvc-ninja --config Release --target TriangleSample --jobs 4
```

### Linux

```bash
bash scripts/cglab.sh configure --preset linux-clang-ninja --config Release
bash scripts/cglab.sh build --preset linux-clang-ninja --config Release --target TriangleSample --jobs 4
```

两个现代 app target 是：

- `TriangleSample`：最小 Vulkan 三角形，用于验证窗口、runtime、backend 和帧循环；
- `GltfSponzaSample`：glTF/GLB app。正常运行时使用 `--asset` 指定模型；没有外部 Sponza 时可以使用仓库内的 `assets/triangle.gltf` 验证加载链路。

例如构建后手动运行 glTF app：

```powershell
.\build\windows-msvc-ninja\Release\GltfSponzaSample.exe --asset C:\data\Sponza\scene.gltf
```

路径中包含空格时按 PowerShell 的规则引用路径。

## 6. 编译所有默认 target

```powershell
.\scripts\cglab.ps1 build --preset windows-msvc-ninja --config Release --target all --jobs 8
```

```bash
bash scripts/cglab.sh build --preset linux-clang-ninja --config Release --target all --jobs 8
```

`all` 会构建当前配置中加入默认构建的 library、两个现代 app 以及已启用的测试 target。默认情况下 tests 和 legacy 都是关闭的，所以通常会得到 `TriangleSample` 和 `GltfSponzaSample`，不会得到 `VulkanSampleLegacy` 或测试可执行文件。

如果只关心某一个 app，显式传入 target 会更快。查看当前 build tree 中所有可用 target：

```powershell
cmake --build build\windows-msvc-ninja\Release --target help
```

## 7. 测试、Smoke 和 Legacy

构建并运行所有 CTest：

```powershell
.\scripts\cglab.ps1 test --preset windows-msvc-ninja --config Debug --jobs 4
```

```bash
bash scripts/cglab.sh test --preset linux-clang-ninja --config Debug --jobs 4
```

Triangle GPU smoke：

```powershell
.\scripts\cglab.ps1 smoke --preset windows-msvc-ninja --config Release --target TriangleSample --frames 6
```

glTF GPU smoke：

```powershell
.\scripts\cglab.ps1 smoke --preset windows-msvc-ninja --config Release --target GltfSponzaSample --asset assets/triangle.gltf --frames 6
```

省略 glTF smoke 的 `--asset` 时，脚本会自动使用 `assets/triangle.gltf`；非 smoke 的 `GltfSponzaSample` 运行仍应显式提供资产路径。

历史快照默认关闭，只有需要验证兼容性时才启用：

```powershell
.\scripts\cglab.ps1 configure --preset windows-msvc-ninja --config Debug --legacy ON
.\scripts\cglab.ps1 build --preset windows-msvc-ninja --config Debug --target VulkanSampleLegacy --legacy ON
```

## 8. VS Code 工作流

不安装扩展也可以使用脚本和 tasks。打开仓库后按 `Ctrl+Shift+P`，运行 `Tasks: Run Task`，常用任务包括：

- `[CGLab] Setup workspace`；
- `[CGLab] Diagnose environment`；
- `[CGLab] Configure` / `[CGLab] Configure fresh`；
- `[CGLab] Build selected target`；
- `[CGLab] Build arbitrary target`；
- `[CGLab] Test all`；
- 两个 GPU smoke 任务；
- `[CGLab] Clean current preset`。

执行构建任务时可以依次选择平台 preset、Debug/Release/RelWithDebInfo、是否启用 tests、target 和并行任务数。常用 target 下拉包含 `all`、两个 app 和主要测试；新增 target 不需要修改 tasks，可以使用 arbitrary target 输入框。

仓库通过 `.vscode/extensions.json` 推荐 CMake Tools 和 C/C++ 扩展，但不依赖它们。安装 CMake Tools 后，可以在扩展面板中选择 configure preset 和 CMake target；命令行脚本仍然是跨平台、可复现的主路径。

## 9. 用户自定义配置

不要修改提交的 `CMakePresets.json`。需要自定义编译器、生成器、build 目录、triplet 或额外 cache variables 时：

```powershell
Copy-Item CMakeUserPresets.json.example CMakeUserPresets.json
```

然后编辑未提交的 `CMakeUserPresets.json`，从仓库 preset 继承并覆盖本机设置。这个文件已被 `.gitignore` 忽略。若自定义了 preset 名称，传入完整名称即可：

```powershell
.\scripts\cglab.ps1 configure --preset local-windows-msvc-ninja-debug --fresh
.\scripts\cglab.ps1 build --preset local-windows-msvc-ninja-debug --target TriangleSample
```

常用环境变量也可以用于本地覆盖：

- `VCPKG_ROOT`：vcpkg checkout 路径；
- `VULKAN_SDK`：Vulkan SDK 路径；
- `CGLAB_PRESET`：默认 preset；
- `CGLAB_CONFIG`：默认构建配置；
- `CGLAB_BUILD_DIR`：自定义 smoke 查找和输出目录。

覆盖优先级是：命令行参数 > `CMakeUserPresets.json` > 环境变量 > 仓库默认值。`--triplet` 和 `--vcpkg-root` 只影响本次依赖安装或配置，不会修改提交的 preset。

## 10. 清理和故障排查

只清理当前 preset 的 CMake 产物：

```powershell
.\scripts\cglab.ps1 clean --preset windows-msvc-ninja --config Debug
```

更换 Visual Studio、编译器、triplet 或用户 preset 后，使用 `configure --fresh`，不要把不同工具链混在同一个 binary directory 中。默认 build tree 是按平台和配置分开的，通常不需要删除整个 `build/`。

常见错误的处理方式：

- CMake 报 vcpkg toolchain 不存在：先执行 `setup`，或者用 `deps` 只初始化依赖；
- Ninja、CMake 或编译器缺失：执行 `doctor`，按照输出的当前平台安装建议补齐工具；
- 从旧版本构建脚本升级后，若 Windows 链接仍出现新旧命名空间或类型不一致的 `LNK2001`，先对受影响配置执行一次 `clean` 再重建；旧 build tree 可能没有保存可用的 MSVC 头文件依赖，清理一次后后续增量构建会正常追踪；
- vcpkg 下载文件 hash 不匹配：不要绕过 hash 校验，先确认仓库中的 manifest/override 和 vcpkg baseline，再重试依赖安装；
- Vulkan headers 能编译但 smoke 失败：检查显卡驱动、`vulkaninfo` 和可用设备；
- shader 需要重新编译但找不到 `glslc`：安装 Vulkan SDK 并设置 `VULKAN_SDK`，然后重新 configure。

## 11. 完成状态

一次干净的默认初始化和构建，预期至少得到：

```text
build/<preset>/<config>/TriangleSample[.exe]
build/<preset>/<config>/GltfSponzaSample[.exe]
```

测试 executable、legacy executable 和其他可选 sample 只有在对应的 CMake 选项或显式 target 被启用/构建时才会出现。这样新 clone 的默认结果不会被历史 smoke 或测试 build 产物污染。
