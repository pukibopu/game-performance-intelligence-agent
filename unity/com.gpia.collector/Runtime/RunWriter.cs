using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace Gpia.Collector
{
    // 把一次采集写成 run 目录，格式必须与 frames.csv 和 run-manifest 的 v1 契约一致
    internal static class RunWriter
    {
        public const string SchemaVersion = "1.1";

        sealed class Column
        {
            public readonly string Name;
            public readonly string Unit;
            public readonly bool Required;
            public readonly Func<FrameSample, string> Format; // 返回 null 表示这一帧缺失

            public Column(string name, string unit, bool required, Func<FrameSample, string> format)
            {
                Name = name;
                Unit = unit;
                Required = required;
                Format = format;
            }
        }

        // 列的顺序就是 CSV 的写出顺序，必需列永远有值
        static readonly Column[] Columns =
        {
            new Column("frame_index", "index", true, s => Int(s.FrameIndex)),
            new Column("timestamp_ns", "ns", true, s => Int(s.TimestampNs)),
            new Column("frame_time_ms", "ms", true, s => Ms(s.FrameTimeMs)),
            new Column("cpu_main_thread_ms", "ms", false, s => Ms(s.CpuMainThreadMs)),
            new Column("cpu_render_thread_ms", "ms", false, s => Ms(s.CpuRenderThreadMs)),
            new Column("cpu_present_wait_ms", "ms", false, s => Ms(s.CpuPresentWaitMs)),
            new Column("gpu_frame_time_ms", "ms", false, s => Ms(s.GpuFrameTimeMs)),
            new Column("gc_alloc_bytes", "bytes", false, s => Int(s.GcAllocBytes)),
            new Column("gc_used_bytes", "bytes", false, s => Int(s.GcUsedBytes)),
            new Column("gc_reserved_bytes", "bytes", false, s => Int(s.GcReservedBytes)),
            new Column("gc_collection_count", "count", false, s => Int(s.GcCollectionCount)),
            new Column("total_used_bytes", "bytes", false, s => Int(s.TotalUsedBytes)),
            new Column("total_reserved_bytes", "bytes", false, s => Int(s.TotalReservedBytes)),
            new Column("system_used_bytes", "bytes", false, s => Int(s.SystemUsedBytes)),
            new Column("draw_calls", "count", false, s => Int(s.DrawCalls)),
            new Column("setpass_calls", "count", false, s => Int(s.SetPassCalls)),
            new Column("batches", "count", false, s => Int(s.Batches)),
            new Column("triangles", "count", false, s => Int(s.Triangles)),
            new Column("vertices", "count", false, s => Int(s.Vertices)),
            new Column("unity_frame_count", "index", false, s => Int(s.UnityFrameCount)),
            new Column("cpu_frame_time_ms", "ms", false, s => Ms(s.CpuFrameTimeMs)),
            new Column("gc_collect_ms", "ms", false, s => Ms(s.GcCollectMs)),
            new Column("scenario_marker", "id", true, s => s.ScenarioMarker.ToString(CultureInfo.InvariantCulture)),
        };

        public static int ColumnCount => Columns.Length;

        public static string[] ColumnNames()
        {
            return Array.ConvertAll(Columns, c => c.Name);
        }

        public static byte[] FormatFrames(FrameSample[] samples, long count, bool[] present)
        {
            var sb = new StringBuilder((int)Math.Min(int.MaxValue, 64 + count * 180));
            sb.Append(string.Join(",", ColumnNames())).Append('\n');
            for (long i = 0; i < count; i++)
            {
                FrameSample s = samples[i];
                for (int c = 0; c < Columns.Length; c++)
                {
                    if (c > 0)
                    {
                        sb.Append(',');
                    }
                    if (present[c])
                    {
                        sb.Append(Columns[c].Format(s));
                    }
                }
                sb.Append('\n');
            }
            return new UTF8Encoding(false).GetBytes(sb.ToString());
        }

        // 统计每一列是否至少有一个值，整列为空时在 manifest 里标为 unsupported
        public static bool[] DetectPresentColumns(FrameSample[] samples, long count)
        {
            var present = new bool[Columns.Length];
            for (int c = 0; c < Columns.Length; c++)
            {
                present[c] = Columns[c].Required && count > 0;
                for (long i = 0; i < count && !present[c]; i++)
                {
                    present[c] = Columns[c].Format(samples[i]) != null;
                }
            }
            return present;
        }

        static string Ms(double value)
        {
            return double.IsNaN(value) ? null : value.ToString("F3", CultureInfo.InvariantCulture);
        }

        static string Int(long value)
        {
            return value < 0 ? null : value.ToString(CultureInfo.InvariantCulture);
        }

        public static string Sha256Hex(byte[] data)
        {
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(data);
                var sb = new StringBuilder(64);
                foreach (byte b in hash)
                {
                    sb.Append(b.ToString("x2", CultureInfo.InvariantCulture));
                }
                return sb.ToString();
            }
        }

        public static string FormatManifest(ManifestData m, bool[] present)
        {
            var sb = new StringBuilder();
            sb.Append("{\n");
            Field(sb, "schema_version", Json.Quote(SchemaVersion));
            Field(sb, "run_id", Json.Quote(m.RunId));
            Field(sb, "status", Json.Quote(m.Aborted ? "aborted" : "completed"));
            Field(sb, "abort_reason", Json.Quote(m.AbortReason));
            Field(sb, "context", Json.Quote(m.Env.IsEditor ? "editor" : "player"));
            Field(sb, "started_at_utc", Json.Quote(m.StartedUtc.ToString("yyyy-MM-ddTHH:mm:ss.fffZ", CultureInfo.InvariantCulture)));
            Field(sb, "ended_at_utc", Json.Quote(m.EndedUtc.ToString("yyyy-MM-ddTHH:mm:ss.fffZ", CultureInfo.InvariantCulture)));
            Field(sb, "frame_count", Json.Number(m.FrameCount));
            Field(sb, "warmup_frames", Json.Number(m.WarmupFrames));
            Field(sb, "frames_file", Json.Quote("frames.csv"));
            Field(sb, "frames_sha256", Json.Quote(m.FramesSha256));
            Field(sb, "build", "{\n" +
                Pair("unity_version", Json.Quote(m.Env.UnityVersion)) + ",\n" +
                Pair("build_id", Json.Quote(m.Env.BuildId)) + ",\n" +
                Pair("development_build", Json.Bool(m.Env.DevelopmentBuild)) + ",\n" +
                Pair("scripting_backend", Json.Quote(m.Env.ScriptingBackend)) + ",\n" +
                Pair("graphics_api", Json.Quote(m.Env.GraphicsApi)) + ",\n" +
                Pair("render_pipeline", Json.Quote(m.Env.RenderPipeline)) + ",\n" +
                Pair("frame_timing_stats", Json.Bool(m.Env.FrameTimingStats)) + ",\n" +
                Pair("incremental_gc", Json.Bool(m.Env.IncrementalGc)) + "\n  }");
            Field(sb, "device", "{\n" +
                Pair("os", Json.Quote(m.Env.Os)) + ",\n" +
                Pair("cpu", Json.Quote(m.Env.Cpu)) + ",\n" +
                Pair("cpu_logical_cores", Json.Number(m.Env.CpuLogicalCores)) + ",\n" +
                Pair("system_memory_mb", Json.Number(m.Env.SystemMemoryMb)) + ",\n" +
                Pair("gpu_name", Json.Quote(m.Env.GpuName)) + ",\n" +
                Pair("gpu_memory_mb", Json.Number(m.Env.GpuMemoryMb)) + ",\n" +
                Pair("gpu_driver", "null") + "\n  }");
            Field(sb, "display", "{\n" +
                Pair("width", Json.Number(m.Env.Width)) + ",\n" +
                Pair("height", Json.Number(m.Env.Height)) + ",\n" +
                Pair("fullscreen_mode", Json.Quote(m.Env.FullscreenMode)) + ",\n" +
                Pair("vsync_count", Json.Number(m.Env.VSyncCount)) + ",\n" +
                Pair("target_frame_rate", Json.Number(m.Env.TargetFrameRate)) + ",\n" +
                Pair("quality_level", Json.Quote(m.Env.QualityLevel)) + "\n  }");

            var parameters = new List<string>();
            foreach (KeyValuePair<string, string> p in m.ScenarioParams)
            {
                parameters.Add($"{Json.Quote(p.Key)}: {Json.ScalarFromText(p.Value)}");
            }
            Field(sb, "scenario", "{\n" +
                Pair("name", Json.Quote(m.ScenarioName)) + ",\n" +
                Pair("seed", Json.Number(m.Seed)) + ",\n" +
                Pair("params", "{" + string.Join(", ", parameters) + "}") + "\n  }");
            Field(sb, "instrumentation", "[" + string.Join(", ", m.Instrumentation.ConvertAll(Json.Quote)) + "]");

            var columns = new List<string>();
            for (int i = 0; i < Columns.Length; i++)
            {
                columns.Add("    {\"name\": " + Json.Quote(Columns[i].Name) +
                            ", \"unit\": " + Json.Quote(Columns[i].Unit) +
                            ", \"availability\": " + Json.Quote(present[i] ? "measured" : "unsupported") +
                            ", \"source\": " + Json.Quote(m.ColumnSources[i]) + "}");
            }
            sb.Append("  \"columns\": [\n").Append(string.Join(",\n", columns)).Append("\n  ]\n}\n");
            return sb.ToString();
        }

        static void Field(StringBuilder sb, string key, string json)
        {
            sb.Append("  ").Append(Json.Quote(key)).Append(": ").Append(json).Append(",\n");
        }

        static string Pair(string key, string json)
        {
            return "    " + Json.Quote(key) + ": " + json;
        }

    }

    internal sealed class ManifestData
    {
        public string RunId;
        public bool Aborted;
        public string AbortReason;
        public DateTime StartedUtc;
        public DateTime EndedUtc;
        public long FrameCount;
        public long WarmupFrames;
        public string FramesSha256;
        public EnvironmentInfo Env;
        public string ScenarioName;
        public int Seed;
        public IReadOnlyDictionary<string, string> ScenarioParams;
        public List<string> Instrumentation = new List<string>();
        public string[] ColumnSources;
    }
}
