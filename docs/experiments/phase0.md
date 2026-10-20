# Phase 0 实验记录

环境：Windows 11 Pro 10.0.26200，Intel i7-10875H 8C/16T，NVIDIA RTX 2070 Super 8 GB（驱动 591.74），
Unity 2022.3.62f3，URP 14.0.12，DX11，窗口模式 1280×720，`vSyncCount=0`，`targetFrameRate=60`。
笔记本接电源，实验期间机器上仍有日常程序在运行，所以结果代表「真实桌面环境」而不是隔离环境。

复现：`uv run tools\run_experiments.py`。原始 run 目录在 `runs/`（不提交），三份代表性样本在 `datasets/golden/`。

## 结论速览

| ID | 问题 | 结论 |
|---|---|---|
| U1 | ProfilerRecorder 在各构建里能拿到哪些条目 | Release 44 条，Development 约 1940 条；完整对照见 [unity-counters-2022.3.md](../unity-counters-2022.3.md) |
| U2 | `GC.Collect` marker 能否记录耗时 | **Mono 的 Release 与 Development 都能**，记为 frames v1.1 的 `gc_collect_ms`；**IL2CPP 找得到 marker 但从不产生样本** |
| U3 | `GC.CollectionCount(0)` 的增量能否定位回收帧 | 能，与注入标记和 `GC.Collect` 非零帧完全同帧 |
| U4 | FrameTimingManager 在 DX11 上是否给出 GPU 时间、延迟多少 | GPU 时间有值；在帧首采样点上**落后 5 帧**，比官方文档说的 4 帧多 1 帧；`frameStartTimestamp` 与 Stopwatch 不同源，按时间戳对齐失败，改用经过验证的固定 5 帧回填 |
| U6 | 采集器的观察者效应 | 回调平均 7–13 µs/帧，约占 60 帧预算的 0.06%；稳态托管分配 0 B/帧；偶发最大 0.1–1.8 ms，集中在启动阶段 |
| U7 | non-dev 构建里能否近似每帧分配量 | 不能用 `gc_used_bytes` 的差值代替：大分配会在同一帧触发回收，差值反而为负；保持 `gc_alloc_bytes` 只在 Development 构建中可用 |
| U8 | 子系统 marker 能否用于 Release 基线 | Release 只有 PlayerLoop、Main Thread、Gfx.WaitForPresentOnGfxThread、WaitForTargetFPS、GC.Collect；脚本、物理、渲染拆分只在 Development 构建可用 |

## 采样时序的修正

注入标记让时序问题可以被精确验证，Phase 0 共发现并修正了三处：

1. **ProfilerRecorder 晚一帧**：在帧首读取的 `LastValue` 是再往前一帧的值。修正前 8 MiB 分配出现在标记帧的下一帧，修正后分配、回收次数、`GC.Collect` 与标记全部同帧。
2. **FrameTiming 晚五帧**：修正前 CPU 尖峰的 `cpu_main_thread_ms` 出现在标记帧之后一帧，固定滞后从 4 改为 5 后完全对齐。
3. **计数器名称**：官方表格写的 `Total Batches Count` 在 Player 中不存在，实际名称是 `Batches Count`。

## 实验矩阵结果

每次运行预热 3 s、测量 20 s，场景每 120 个测量帧注入一次负载，共 10 次。
精确率与召回率以注入标记为 ground truth，卡顿由 `gpia-core` 的默认规则判定：帧时间超过 max(1.5 × 滚动中位数, 1.5 × 帧预算)。

| 实验 | 运行数 | P50 ms | P99 ms | 最大 ms | 注入 | 检出卡顿 | 精确率 | 召回率 | GC 卡顿的证据等级 | 回调 µs 平均 / 最大 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| idle Mono Release | 1 | 16.667 | 16.956 | 17.690 | 0 | 0 | — | — | — | 10.6 / 905 |
| idle Mono Dev | 1 | 16.667 | 16.963 | 17.181 | 0 | 0 | — | — | — | 11.8 / 777 |
| cpu_spike Mono Release | 3 | 16.667 | 17.333 | 30.946 | 30 | 30 | 1.00 | 1.00 | — | 10.3 / 878 |
| cpu_spike IL2CPP Release | 3 | 16.667 | 17.375 | 31.320 | 30 | 30 | 1.00 | 1.00 | — | 7.5 / 376 |
| gc 强制回收 Mono Dev | 3 | 16.667 | 17.398 | 76.984 | 30 | 30 | 1.00 | 1.00 | evidence_backed_diagnosis × 30 | 12.6 / 1785 |
| gc 强制回收 Mono Release | 3 | 16.667 | 17.317 | 69.903 | 30 | 30 | 1.00 | 1.00 | evidence_backed_diagnosis × 30 | 10.0 / 870 |
| gc 强制回收 IL2CPP Release | 3 | 16.667 | 17.863 | 85.086 | 30 | 31 | 0.97 | 1.00 | hypothesis × 30 | 7.8 / 129 |
| gc 增量回收 Mono Dev | 1 | 16.667 | 16.981 | 18.880 | 10 次分配 | 0 | — | — | — | 12.0 / 847 |

说明：

- **GC 场景的校准**：第一轮用 200 万个常驻对象，强制回收只需约 20 ms，帧时间约为中位数的 1.25 倍，低于卡顿阈值，召回率只有 0.20–0.47。这些帧按定义本来就不是卡顿，问题在于注入的负载太弱。改为 500 万个常驻对象后回收需要 45–70 ms，每次注入都是明确的卡顿，于是得到上表。
- **证据等级的差异来自运行时**：Mono 上 `GC.Collect` 实测耗时中位数 49–55 ms，约占超额帧时间的 90%，结论升级为 evidence_backed_diagnosis。IL2CPP 上该 marker 恒为 0，分析器按设计把它视为「覆盖不到这次回收」，结论停在 hypothesis 并写明缺少的证据。
- **IL2CPP 多出的一次卡顿**：第 3 次运行第 1140 帧 32.5 ms，主线程 16.7 ms、等待 Present 15.8 ms、没有回收。这是环境干扰造成的真实卡顿，检测本身没有错。
- **增量 GC**：8 MiB 分配在同帧触发回收，但增量回收把工作分摊到多帧，60 帧上限下没有可见卡顿。这说明「分配量大」不能直接推出「GC 导致卡顿」，与分析器只在同帧有回收证据时才做关联的设计一致。

## Mono 与 IL2CPP 对比

| 指标 | Mono Release | IL2CPP Release |
|---|---:|---:|
| cpu_spike 场景 P99 | 17.333 ms | 17.375 ms |
| 30 ms 忙等尖峰的帧时间中位数 | 30.68 ms | 30.66 ms |
| 强制回收卡顿帧时间中位数 | 59.1 ms | 63.6 ms |
| `GC.Collect` 可作为停顿证据 | 是 | 否 |
| 采集回调平均耗时 | 10.0–10.3 µs | 7.5–7.8 µs |

在本工程的负载下两种后端的帧时间分布几乎相同；IL2CPP 的采集回调更便宜，但失去了 GC 停顿的直接证据，
需要靠 Phase 2 的 Profiler 捕获补上。

## 仍未验证

- `unity_frame_count` 与 Profiler `.raw`、RenderDoc 捕获的帧号对齐方式，留给 Phase 2 的 U5。
- 实验没有隔离后台程序与温度，重复运行之间 P99 的差异在 0.4 ms 以内，Phase 2 做回归比较时需要更严格的环境控制。
