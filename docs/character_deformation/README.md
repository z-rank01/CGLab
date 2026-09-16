# 角色形变研究：蒙皮、实时肌肉与布料

> 状态：规划已建立，代码与资产尚未实施。日期：2026-09-16。
> 分支：`codex/character-deformation-research`。
> 基线：`661ae236`（main）。本目录是本研究分支的执行依据；既有渲染主线的排序仍以 [统一计划](../Plan.md) 为准。

## 1. 目标与边界

在 CGLab 现有渲染框架中建立可验证的角色形变实验环境，先完成 CPU/GPU 蒙皮，再研究离线模拟样例的 SSDR 压缩、RBF 姿态驱动，最终按证据决定是否加入在线动态响应。

三个目标必须分别命名、分别验收：

1. **动态缓存压缩与实时播放**：DCC 连续模拟 → SSDR → 保存逐帧虚拟骨骼变换 → 实时蒙皮播放。
2. **姿态相关形变重建**：准静态样例 → SSDR + RBF → 输入角色姿态 → 输出表面形变。
3. **实时动态响应**：对运动历史、惯性、外力和碰撞产生响应。需要显式状态或在线物理，不能仅用当前姿态的 RBF 保证。

同一手臂姿态可能对应抬手中的袖子、放手中的袖子或静止袖子。相同关节角度也可能对应不同肌肉激活和负载。因此，本研究不把“播放带有动态的烘焙结果”标为“实时物理”，也不把人工鼓包 corrective 标为解剖学肌肉模拟。

首个完整研究目标：**单臂与简单袖筒，显示 ground truth、SSDR 重建和 RBF 重建，支持误差热图与未见姿态验证。** 皮肤与布料分别建模，首版不做全身、多层衣物、自碰撞复杂裙摆。

## 2. 文档导航与当前行动

| 文档 | 内容 |
|---|---|
| [AssetPreparation.md](AssetPreparation.md) | 用户当前先做的工作：选择、制作、导出与验收必要资产 |
| [ExecutionPlan.md](ExecutionPlan.md) | S0–S5 蒙皮前置任务、D0–D5 研究步骤、实现边界、验收与回归 |

**下一步是 A0–A2 资产准备，不直接开始 SSDR/RBF。** 最先需要的不是完整高质量角色，而是一个可信的两骨骼测试网格和一个有明确导出设置的蒙皮人偶。

## 3. 仓库基线评估

以下为代码与文档检查所得，未在建立本计划时重新构建或运行 GPU；历史测试结果不当作本次验证结果。

| 能力 | 当前状态及入口 | 研究中的用法 |
|---|---|---|
| 应用与帧循环 | `src/apps/application_runner.*`、`src/engine/engine_runtime.*` | 新增独立 sample |
| 静态资产 | `third_party/digital-content-loader/src/asset_database.h`、`src/asset/` | 保留静态路径；角色数据需新增 |
| 现有顶点 | position/color/normal/tangent/uv0/uv1 | 没有 joint indices/weights |
| 动画数据 | DCL 没有 skin、animation、morph 输出表 | S4 增加受控 glTF 子集 |
| 当前 shader | `src/shader/gltf.vert` | 仅对象变换，尚无蒙皮 |
| 帧通道 | `src/engine/frame_channels.h` | 发布姿态/palette/动态顶点等专用数据 |
| 上传与绘制 | `src/apps/*_render_recipe.cpp` | 可复用 buffer upload；没有现成角色动态网格链路 |
| Compute | RG `render_device.h`、Vulkan pipeline/dispatch 实现 | 已有基础；compute skinning 后置 |
| 调试与性能 | Web 控制平面、暂停/单步、遥测、job system | 按需求扩展，不重建整套框架 |
| 渲染质量 | 阴影、PBR、后处理等 | 已足够实验；IBL/CSM 不是前置 |

实施前重新确认上述文件，尤其在同步主线之后。README 与旧路线文档可能落后于实际代码。

## 4. 分工与学习边界

| 内容 | 所有者 | AI 可做的协助 |
|---|---|---|
| 空间约定、FK、palette、CPU/GPU LBS | 用户亲自实现 | 推导解释、接口建议、代码评审、独立测试用例 |
| SSDR 目标、约束、交替优化、初始化 | 用户亲自实现 | 原论文解读、退化用例、误差可视化、与基准比较 |
| RBF 特征、拟合、推理与回退 | 用户亲自实现 | 数值诊断、实验设计、评审 |
| 后续 XPBD/动态核心 | 用户亲自实现 | 推导、稳定性与碰撞案例检查 |
| CMake、sample/recipe 接线、资源生命周期 | AI 可实施 | 用户审核关键契约 |
| DCC 脚本、批量采样、烘焙、导出器 | AI 可实施或辅助 | 用户确认实际版本与视觉质量 |
| 数据校验、格式转换、报告、UI | AI 可实施 | 不改写核心算法来迁就测试 |

默认不让 AI 直接替用户完成核心算法。具体协作时先声明本轮允许修改的模块。使用成熟线性代数库求 SVD/QR 不影响用户实现算法主体；无需为了学习 SSDR 重写底层矩阵分解。

## 5. 工程纪律

- 新实验优先独立 sample、专用 recipe 和独立模块，避免长期复制并改写 glTF 大 recipe。
- API 无关的算法与数据不包含 Vulkan；GPU 副作用仍归 RG Vulkan backend。
- 保持现有 SoA/CSR、扁平数据、错误返回、稳态内存复用约定；遵守 `cmake/ArchitectureContract.cmake`。
- 初期通过专用帧通道接线，验证后再决定哪些接口上升到 Engine。不能用缺乏所有权的局部对象指针发布帧数据。
- DCL/RG 若需改动，在各自子仓分支独立提交，再由主仓提交指针；不把子仓变更混成无法复现的工作树状态。
- 文档与资产准备不要求运行无关构建；代码阶段按变更运行有意义的测试与 GPU validation。
- 大缓存与 DCC 自动备份不直接提交普通 Git。提交生成脚本、清单、许可证、校验信息及小型回归样例；大数据位置与重建方法必须可追溯。
- 分阶段同步渲染主线，通用修复与研究功能分开提交。
- 阶段完成必须记录实际命令、输入、结果、限制和提交号；没有数据的项目保持待验收。

## 6. 非目标与工作量预期

蒙皮基础阶段不要求动画状态机、混合树、IK、重定向、DQS、GPU 动画采样、异步 compute、多队列或完整角色控制器。研究首版不要求 morph 通用导入、全身肌肉 FEM、完整衣物自碰撞或任意体型/材质泛化。

初步投入参考：资产/数据约定 8–16 小时，蒙皮与回放 20–40 小时，DCC 数据闭环 20–50 小时，SSDR 40–80 小时，RBF 24–50 小时，运行时与验收整理 20–40 小时。合计约 130–280 小时，真实 glTF 兼容性、DCC 学习与复杂资产可能额外增加投入；这不是承诺工期。在线动力学与真实肌肉数据升级单独估算。

## 7. 参考资料

以下链接作为原理与工具能力参考，不替代对实际 DCC 版本、资产许可与导出结果的检查。

- [Khronos glTF Skins](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html)
- [Khronos glTF Animations](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_007_Animations.html)
- [SSDR 原始论文](https://graphics.cs.uh.edu/wp-content/papers/2012/2012_SA_SSDR_preprint.pdf)
- [EA Dem Bones：SSDR 对照实现](https://github.com/electronicarts/dem-bones)
- [Pose Space Deformation 原始论文](https://www.scribblethink.org/Work/PSD/PSD.pdf)
- [XPBD 原始论文](https://matthias-research.github.io/pages/publications/XPBD.pdf)
- [SideFX Vellum muscles/tissue](https://www.sidefx.com/docs/houdini/muscles/workflowvellum.html)
- [SideFX Apprentice 限制](https://www.sidefx.com/faq/question/apprentice-restrictions/)
- [Blender demo / assets](https://www.blender.org/download/demo-files/)
- [MakeHuman 许可说明](https://static.makehumancommunity.org/about/license.html)
- [Marvelous Designer Alembic 导入导出](https://support.marvelousdesigner.com/hc/en-us/articles/47358253053081-3D-File-Alembic-Import-Export)

## 8. 状态记录

| 阶段 | 状态 | 证据 |
|---|---|---|
| 规划与分工 | 已建立 | 本目录三个文档 |
| A0–A2 必要资产 | 待用户收集/创建 | 见资产清单 |
| S0–S5 蒙皮基础 | 未开始 | 无实现/测试结论 |
| D0–D5 形变研究 | 未开始 | 无训练/质量/性能结论 |

待定项：实际 DCC 与版本、资产来源、参考坐标系、目标硬件、误差与性能预算。可以先准备资产，但进入实现与正式采样前应在对应文档中记录决策。
