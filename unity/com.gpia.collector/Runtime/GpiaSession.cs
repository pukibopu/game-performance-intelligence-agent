using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using UnityEngine.LowLevel;
using UnityEngine.PlayerLoop;
using Debug = UnityEngine.Debug;

namespace Gpia.Collector
{
    // 一次采集会话，在每帧最开始结算上一帧，整个热路径不分配托管内存
    public static class GpiaSession
    {
        struct GpiaFrameStart
        {
        }

        static FrameSample[] samples;
        static long count;
        static bool overflowed;
        static long origin;
        static long frameStart;
        static bool hasPrevious;
        static uint pendingMarker;
        static double msPerTick;
        static double nsPerTick;

        static MetricRecorders recorders;
        static MarkerRecorders markers;
        static FrameTimingAligner frameTiming;

        static RunOptions options;
        static string scenarioName;
        static DateTime startedUtc;
        static long warmupFrames;
        static EnvironmentInfo environment;

        public static bool IsRecording { get; private set; }

        public static long FramesRecorded => count;

        public static void Begin(RunOptions runOptions, string scenario)
        {
            if (IsRecording)
            {
                throw new InvalidOperationException("a GPIA session is already recording");
            }
            options = runOptions;
            scenarioName = scenario;
            double seconds = runOptions.WarmupSeconds + runOptions.DurationSeconds + 5.0;
            int capacity = (int)Math.Min(int.MaxValue - 1, Math.Ceiling(seconds * runOptions.MaxFrameRate));
            samples = new FrameSample[capacity];
            count = 0;
            overflowed = false;
            hasPrevious = false;
            pendingMarker = 0;
            warmupFrames = 0;
            msPerTick = 1000.0 / Stopwatch.Frequency;
            nsPerTick = 1e9 / Stopwatch.Frequency;

            recorders = new MetricRecorders();
            markers = runOptions.Markers.Count > 0 ? new MarkerRecorders(runOptions.Markers, capacity) : null;
            frameTiming = new FrameTimingAligner(capacity, runOptions.FrameTimingLag);
            environment = EnvironmentInfo.Capture();
            startedUtc = DateTime.UtcNow;
            origin = Stopwatch.GetTimestamp();
            Install();
            IsRecording = true;
        }

        // 场景在 Update 里调用，标记会落到当前这一帧的采样上
        public static void MarkFrame(uint marker)
        {
            pendingMarker = marker;
        }

        public static void EndWarmup()
        {
            warmupFrames = count;
        }

        // 写出 run 目录并返回其路径，中途失败时把状态记为 aborted
        public static string End(bool aborted, string abortReason)
        {
            if (!IsRecording)
            {
                throw new InvalidOperationException("no GPIA session is recording");
            }
            Uninstall();
            IsRecording = false;
            DateTime endedUtc = DateTime.UtcNow;
            recorders.Dispose();
            if (overflowed && !aborted)
            {
                aborted = true;
                abortReason = $"sample buffer full after {count} frames, raise -gpia-max-fps";
            }

            string runId = string.IsNullOrEmpty(options.RunId) ? MakeRunId(startedUtc, scenarioName, environment) : options.RunId;
            string runDir = Path.Combine(options.OutDir, runId);
            Directory.CreateDirectory(runDir);

            bool[] present = RunWriter.DetectPresentColumns(samples, count);
            byte[] frames = RunWriter.FormatFrames(samples, count, present);
            File.WriteAllBytes(Path.Combine(runDir, "frames.csv"), frames);

            var manifest = new ManifestData
            {
                RunId = runId,
                Aborted = aborted,
                AbortReason = aborted ? abortReason ?? "unknown" : null,
                StartedUtc = startedUtc,
                EndedUtc = endedUtc,
                FrameCount = count,
                WarmupFrames = warmupFrames,
                FramesSha256 = RunWriter.Sha256Hex(frames),
                Env = environment,
                ScenarioName = scenarioName,
                Seed = options.Seed,
                ScenarioParams = options.Params,
                ColumnSources = ColumnSources(frameTiming.Method),
            };
            File.WriteAllText(Path.Combine(runDir, "manifest.json"), RunWriter.FormatManifest(manifest, present),
                new UTF8Encoding(false));

            if (markers != null)
            {
                markers.Write(runDir, count);
                markers.Dispose();
            }
            Debug.Log($"[GPIA] wrote {count} frames to {runDir}; frame timing matched {frameTiming.Matched}, " +
                      $"fallback {frameTiming.FallbackAssigned}, unplaced {frameTiming.Unplaced}");
            samples = null;
            return runDir;
        }

        static void OnFrameStart()
        {
            long now = Stopwatch.GetTimestamp();
            if (hasPrevious)
            {
                if (count < samples.Length)
                {
                    // 实测 ProfilerRecorder 在下一帧开始时才给出上一帧之前那一帧的值，所以回填到前一个采样
                    if (count > 0)
                    {
                        recorders.Fill(ref samples[count - 1]);
                        markers?.Record(count - 1);
                    }
                    ref FrameSample s = ref samples[count];
                    s = FrameSample.Empty(count);
                    s.FrameTimeMs = (now - frameStart) * msPerTick;
                    s.TimestampNs = (long)((now - origin) * nsPerTick);
                    s.GcCollectionCount = GC.CollectionCount(0);
                    // 回调排在 Initialization 最前面，此时 Unity 还没把帧号推进到新的一帧
                    s.UnityFrameCount = UnityEngine.Time.frameCount;
                    s.ScenarioMarker = pendingMarker;
                    frameTiming.RecordFrameStart(count, frameStart);
                    count++;
                    frameTiming.Capture(samples, count);
                }
                else
                {
                    overflowed = true;
                }
            }
            pendingMarker = 0;
            hasPrevious = true;
            frameStart = now;
        }

        static void Install()
        {
            PlayerLoopSystem loop = PlayerLoop.GetCurrentPlayerLoop();
            for (int i = 0; i < loop.subSystemList.Length; i++)
            {
                if (loop.subSystemList[i].type != typeof(Initialization))
                {
                    continue;
                }
                PlayerLoopSystem init = loop.subSystemList[i];
                var systems = new List<PlayerLoopSystem>(init.subSystemList ?? Array.Empty<PlayerLoopSystem>());
                systems.Insert(0, new PlayerLoopSystem { type = typeof(GpiaFrameStart), updateDelegate = OnFrameStart });
                init.subSystemList = systems.ToArray();
                loop.subSystemList[i] = init;
                PlayerLoop.SetPlayerLoop(loop);
                return;
            }
            throw new InvalidOperationException("PlayerLoop has no Initialization phase");
        }

        static void Uninstall()
        {
            PlayerLoopSystem loop = PlayerLoop.GetCurrentPlayerLoop();
            for (int i = 0; i < loop.subSystemList.Length; i++)
            {
                if (loop.subSystemList[i].type != typeof(Initialization))
                {
                    continue;
                }
                PlayerLoopSystem init = loop.subSystemList[i];
                var systems = new List<PlayerLoopSystem>(init.subSystemList);
                systems.RemoveAll(s => s.type == typeof(GpiaFrameStart));
                init.subSystemList = systems.ToArray();
                loop.subSystemList[i] = init;
                PlayerLoop.SetPlayerLoop(loop);
                return;
            }
        }

        static string MakeRunId(DateTime started, string scenario, EnvironmentInfo env)
        {
            string raw = $"{started:yyyyMMddTHHmmss}Z-{scenario}-{env.ScriptingBackend}-{(env.DevelopmentBuild ? "dev" : "rel")}";
            var sb = new StringBuilder(raw.Length);
            foreach (char c in raw)
            {
                sb.Append(char.IsLetterOrDigit(c) && c < 0x80 || c == '_' || c == '-' ? c : '_');
            }
            return sb.Length > 64 ? sb.ToString(0, 64) : sb.ToString();
        }

        static string[] ColumnSources(string frameTimingMethod)
        {
            string timing = "FrameTimingManager aligned by " + frameTimingMethod;
            return new[]
            {
                "collector",
                "Stopwatch at frame start",
                "Stopwatch delta between frame starts",
                timing + ": cpuMainThreadFrameTime",
                timing + ": cpuRenderThreadFrameTime",
                timing + ": cpuMainThreadPresentWaitTime",
                timing + ": gpuFrameTime",
                "ProfilerRecorder:GC Allocated In Frame",
                "ProfilerRecorder:GC Used Memory",
                "ProfilerRecorder:GC Reserved Memory",
                "System.GC.CollectionCount(0)",
                "ProfilerRecorder:Total Used Memory",
                "ProfilerRecorder:Total Reserved Memory",
                "ProfilerRecorder:System Used Memory",
                "ProfilerRecorder:Draw Calls Count",
                "ProfilerRecorder:SetPass Calls Count",
                "ProfilerRecorder:Batches Count",
                "ProfilerRecorder:Triangles Count",
                "ProfilerRecorder:Vertices Count",
                "Time.frameCount at frame start",
                timing + ": cpuFrameTime",
                "ProfilerRecorder:GC.Collect marker",
                "scenario:" + scenarioName,
            };
        }
    }
}
