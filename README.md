# GPIA — Game Performance Intelligence Agent

> Performance First, Evidence Driven, Agent Assisted.

游戏性能智能分析 Agent：Unity C# 采集 → C++20 确定性分析 → Python MCP + LangGraph 智能诊断。

## Status
Phase 0 (Unity + C++ Performance PoC) — in progress.

## Layout (grows by phase)
| Path | Purpose |
|---|---|
| `schemas/` | 跨语言数据契约（JSON Schema） |
| `core/` | C++20 Performance Analysis Core + `gpia-core` CLI |
| `unity/` | Unity Collector 包 + Benchmark 项目 |
| `docs/` | 计划、ADR、实验记录 |

## License
MIT