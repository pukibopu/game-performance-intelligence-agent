# GPIA — Game Performance Intelligence Agent

> Performance First, Evidence Driven, Agent Assisted.

游戏性能智能分析 Agent：Unity C# 采集 → C++20 确定性分析 → Python MCP + LangGraph 智能诊断。
先拿到真实的性能数据，再用确定性算法找问题，最后才让 Agent 做诊断；没有 LLM 时整条链路照样能用。

## Status

**Phase 0 已完成**：Unity 2022.3 Standalone Player → C# Collector → frames.csv + manifest.json → C++20 `gpia-core` → 带证据等级的 JSON / Markdown 报告。
Phase 1（MCP Server + LangGraph Agent）尚未开始。

## 架构

```
Unity Player (C#)                          C++20 gpia-core                         报告
┌───────────────────────────┐   run 目录   ┌─────────────────────────────┐        ┌──────────────────────┐
│ PlayerLoop 帧首回调        │ ──────────▶ │ manifest 校验 + SHA-256      │ ─────▶ │ report.json (schema)  │
│ ProfilerRecorder 计数器    │ manifest.json│ CSV 解析 + 逐行诊断          │        │ report.md             │
│ FrameTimingManager 回填    │ frames.csv   │ 统计 / 尖峰检测              │        │ evidence + findings   │
│ 场景注入 ground truth 标记 │              │ 帧 / CPU / GC / 内存分析器   │        │ 证据等级 + 缺失证据   │
└───────────────────────────┘              └─────────────────────────────┘        └──────────────────────┘
```

- **证据纪律**：每条结论都引用报告里存在的证据 ID，并带有证据等级
  `measured > deterministic_finding > evidence_backed_diagnosis > hypothesis > insufficient_evidence`。
  例如卡顿帧与 GC 回收同帧只算 hypothesis，只有 `GC.Collect` 实测耗时占到超额帧时间一半以上才升级为 evidence_backed_diagnosis。
- **缺失不补零**：拿不到的指标写成空单元格并在 manifest 里标为 unsupported，统计量缺失时直接省略。

## 目录

| 路径 | 内容 |
|---|---|
| `schemas/` | 跨语言数据契约：`frames.v1.md`、`run-manifest.v1.schema.json`、`analysis-report.v1.schema.json` |
| `core/` | C++20 分析核心与 `gpia-core` 命令行，GoogleTest 单元测试与合成数据集 |
| `unity/com.gpia.collector/` | UPM 采集包，零分配的帧首采样，EditMode 测试 |
| `unity/GpiaBenchmark/` | URP 基准工程：idle、cpu_spike、gc_pressure 场景与批量构建脚本 |
| `tools/` | 合成数据生成、schema 校验、端到端检查、Player 实验矩阵 |
| `datasets/golden/` | 真实 Player 采集的小样本 |
| `docs/` | 实验记录与 Unity 计数器清单 |

## 快速开始

需要 Windows、Visual Studio 2022（含 C++ 桌面开发）、Unity 2022.3.62f3（含 IL2CPP 模块）、Python 3.12 与 uv。

```powershell
# C++ 核心：在 x64 Developer PowerShell 中
cd core
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release

# 分析仓库自带的真实样本
.\build\windows-msvc-release\gpia-core.exe analyze --run ..\datasets\golden\gc_forced_mono_dev --format md
```

```powershell
# Unity：配置工程并构建四个 Player
$unity = "C:\Program Files\Unity\Hub\Editor\2022.3.62f3\Editor\Unity.exe"
& $unity -batchmode -quit -projectPath unity\GpiaBenchmark -executeMethod Gpia.Benchmark.Editor.ProjectSetup.Run
& $unity -batchmode -quit -projectPath unity\GpiaBenchmark -executeMethod Gpia.Benchmark.Editor.BuildPlayers.BuildFromCommandLine

# 采集一次并分析
unity\GpiaBenchmark\Builds\mono-release\GpiaBenchmark.exe -gpia-scenario cpu_spike -gpia-duration 20 -gpia-out runs
```

`gpia-core` 的退出码是调用方契约：0 成功，2 用法错误，3 读取失败，4 输入不合法，5 数据不足，10 内部错误。

## 复现 Phase 0 实验

```powershell
uv run tools\run_experiments.py
```

结果写到 `runs/phase0/summary.md`，结论整理在 [docs/experiments/phase0.md](docs/experiments/phase0.md)。

## License

MIT
