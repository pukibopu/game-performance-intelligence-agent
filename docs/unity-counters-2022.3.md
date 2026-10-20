# Unity 2022.3.62f3 Player 中可用的 ProfilerRecorder 计数器与 marker（实验 U1 / U8）

测量方式：`GpiaBenchmark.exe -gpia-probe-counters <path>` 在场景加载后调用 `ProfilerRecorderHandle.GetAvailable`，
原始清单在 `runs/probe/`（不提交）。Windows 11、DX11、URP 14.0.12、Mono 后端。

| 构建 | 可用条目数 |
|---|---:|
| Mono Release Player | 44 |
| Mono Development Player | 1946 |
| IL2CPP Release Player | 44 |
| IL2CPP Development Player | 1941 |

注意：IL2CPP 构建里 `GC.Collect` marker 同样能被找到，但实测从不产生样本，回收发生时记录值仍为 0，
所以 IL2CPP 上 `gc_collect_ms` 不能作为 GC 停顿的证据，见 [实验记录](experiments/phase0.md)。

## 采集器用到的条目

| 名称 | 类别 | Release | Development | 说明 |
|---|---|:---:|:---:|---|
| GC Allocated In Frame | Memory | ✘ | ✔ | 只在 Development 构建中可用，与官方文档一致 |
| GC Used Memory / GC Reserved Memory | Memory | ✔ | ✔ | |
| Total Used Memory / Total Reserved Memory / System Used Memory | Memory | ✔ | ✔ | |
| Draw Calls Count / SetPass Calls Count | Render | ✔ | ✔ | |
| **Batches Count** | Render | ✔ | ✔ | 官方表格写作 Total Batches Count，但 Player 里不存在这个名字 |
| Triangles Count / Vertices Count | Render | ✔ | ✔ | |
| **GC.Collect**（marker） | GC | ✔ | ✔ | 官方文档没有说明 Release 是否可用，实测可用，记录为 `gc_collect_ms` |
| CPU Main Thread Frame Time / CPU Render Thread Frame Time / GPU Frame Time | Render | ✔ | ✔ | FrameTimingManager 的同源计数器 |

## 实验 U8：候选子系统 marker

| marker | Release | Development |
|---|:---:|:---:|
| PlayerLoop | ✔ | ✔ |
| Main Thread | ✔ | ✔ |
| Gfx.WaitForPresentOnGfxThread | ✔ | ✔ |
| WaitForTargetFPS | ✔ | ✔ |
| GC.Collect | ✔ | ✔ |
| BehaviourUpdate | ✘ | ✔ |
| Physics.Simulate | ✘ | ✔ |
| Camera.Render | ✘ | ✔ |
| Update.ScriptRunBehaviourUpdate | ✘ | ✘ |

结论：子系统级的脚本 / 物理 / 渲染拆分只能在 Development 构建里做，`markers.csv` 因此只属于 profiling 档；
Release 基线只能拿到 PlayerLoop、主线程总量、等待 Present、等待目标帧率和 GC.Collect。

## Release 构建的完整清单

App Committed Memory, App Resident Memory, Audio Reserved Memory, Audio Used Memory, Batches Count,
CPU Main Thread Frame Time, CPU Render Thread Frame Time, CPU Total Frame Time, Draw Calls Count, FrameTime.GPU,
GC Reserved Memory, GC Used Memory, GC.Collect, GPU Frame Time, Gfx.WaitForPresentOnGfxThread,
Index Buffer Upload In Frame Bytes, Index Buffer Upload In Frame Count, Inl_XR Occlusion Mesh, Main Thread,
PlayerLoop, Profiler Reserved Memory, Profiler Used Memory, Render Textures Bytes, Render Textures Changes Count,
Render Textures Count, SetPass Calls Count, Shadow Casters Count, System Total Used Memory, System Used Memory,
Total Reserved Memory, Total Used Memory, Triangles Count, Used Buffers Bytes, Used Buffers Count,
Vertex Buffer Upload In Frame Bytes, Vertex Buffer Upload In Frame Count, Vertices Count, Video Memory Bytes,
Video Reserved Memory, Video Used Memory, Visible Skinned Meshes Count, WaitForTargetFPS, XR Occlusion Mesh，
外加一个未命名的 ProfilerMarker 占位条目。
