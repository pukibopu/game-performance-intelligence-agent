# frames.csv — Column Contract v1

## 格式规则
- UTF-8（无 BOM），**LF 换行**，逗号分隔，第一行是表头。
- 数值使用 invariant culture：小数点是 `.`，不用千位分隔符，不加引号。
- **空单元格 = 该帧没有这个值（Missing）**。禁止用 0 填充缺失值，禁止写 `NaN` / `Inf`。
- 列按**表头名称**识别，与顺序无关。读取方遇到未知列：警告后忽略（向前兼容 minor 版本）。
- 某列在当前构建 / 平台下完全无法采集时：该列仍保留在表头中，所有单元格为空，并在 manifest 中把它标为 `unsupported`。
- Collector 会丢弃第一次采样（没有上一帧，无法计算帧间隔），所以每一行都有有效的 `frame_time_ms`。
- 包含 warmup 帧（不在采集端裁剪），由 manifest 的 `warmup_frames` 告诉分析端从哪一帧开始统计。

## 列定义
| 列名 | 类型 | 单位 | 必需 | 来源（Unity 2022.3） | 说明 |
|---|---|---|---|---|---|
| frame_index | uint64 | index | ✔ | Collector 计数 | 从 0 开始，严格 +1 递增；出现跳号说明丢了样本 |
| timestamp_ns | int64 | ns | ✔ | `Stopwatch.GetTimestamp()` | 从 Collector 启动开始计算，单调递增 |
| frame_time_ms | double | ms | ✔ | 相邻两次 timestamp 的差 | 墙钟帧间隔 |
| cpu_main_thread_ms | double | ms | | `FrameTiming.cpuMainThreadFrameTime` | 有 4 帧延迟并已回填；最后 4 帧通常为空 |
| cpu_render_thread_ms | double | ms | | `FrameTiming.cpuRenderThreadFrameTime` | 同上 |
| cpu_present_wait_ms | double | ms | | `FrameTiming.cpuMainThreadPresentWaitTime` | 同上 |
| gpu_frame_time_ms | double | ms | | `FrameTiming.gpuFrameTime` | 同上 |
| gc_alloc_bytes | int64 | bytes | | Counter「GC Allocated In Frame」 | **只在 Development Build 中可用** |
| gc_used_bytes | int64 | bytes | | Counter「GC Used Memory」 | |
| gc_reserved_bytes | int64 | bytes | | Counter「GC Reserved Memory」 | |
| gc_collection_count | int32 | count | | `System.GC.CollectionCount(0)` | 累计值，分析时取差值（实验 U3 验证） |
| total_used_bytes | int64 | bytes | | Counter「Total Used Memory」 | |
| total_reserved_bytes | int64 | bytes | | Counter「Total Reserved Memory」 | |
| system_used_bytes | int64 | bytes | | Counter「System Used Memory」 | |
| draw_calls | int32 | count | | Counter「Draw Calls Count」 | |
| setpass_calls | int32 | count | | Counter「SetPass Calls Count」 | |
| batches | int32 | count | | Counter「Total Batches Count」 | |
| triangles | int64 | count | | Counter「Triangles Count」 | |
| vertices | int64 | count | | Counter「Vertices Count」 | |
| scenario_marker | uint32 | id | ✔ | Benchmark 场景 | 0 = 无注入；非 0 = 场景在这一帧注入了负载（ground truth） |
