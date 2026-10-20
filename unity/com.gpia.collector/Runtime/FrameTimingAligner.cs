using System.Diagnostics;
using UnityEngine;

namespace Gpia.Collector
{
    // FrameTimingManager 的结果晚几帧才出现，按帧开始时间把它回填到正确的采样上
    internal sealed class FrameTimingAligner
    {
        const double MatchToleranceSeconds = 0.001;

        readonly FrameTiming[] latest = new FrameTiming[1];
        readonly long[] frameStartTicks;
        readonly int fallbackLag;
        readonly double timingSecondsPerTick;
        readonly double stopwatchSecondsPerTick;

        public long Matched { get; private set; }
        public long FallbackAssigned { get; private set; }
        public long Unplaced { get; private set; }

        public FrameTimingAligner(int capacity, int fallbackLag)
        {
            frameStartTicks = new long[capacity];
            this.fallbackLag = fallbackLag;
            ulong frequency = FrameTimingManager.GetCpuTimerFrequency();
            timingSecondsPerTick = frequency > 0 ? 1.0 / frequency : 0.0;
            stopwatchSecondsPerTick = 1.0 / Stopwatch.Frequency;
        }

        public string Method => Matched >= FallbackAssigned
            ? "frameStartTimestamp match"
            : $"fixed lag of {fallbackLag} frames";

        public void RecordFrameStart(long sampleIndex, long startTicks)
        {
            frameStartTicks[sampleIndex] = startTicks;
        }

        // completed 是已经结束的采样数，新数据只会落在其中某一帧上
        public void Capture(FrameSample[] samples, long completed)
        {
            FrameTimingManager.CaptureFrameTimings();
            if (completed == 0 || FrameTimingManager.GetLatestTimings(1, latest) == 0)
            {
                return;
            }
            FrameTiming timing = latest[0];
            long target = FindByStart(completed, timing.frameStartTimestamp * timingSecondsPerTick);
            if (target >= 0)
            {
                Matched++;
            }
            else
            {
                target = completed - fallbackLag;
                if (target < 0)
                {
                    Unplaced++;
                    return;
                }
                FallbackAssigned++;
            }

            ref FrameSample sample = ref samples[target];
            sample.CpuMainThreadMs = Positive(timing.cpuMainThreadFrameTime);
            sample.CpuRenderThreadMs = Positive(timing.cpuRenderThreadFrameTime);
            sample.CpuPresentWaitMs = NonNegative(timing.cpuMainThreadPresentWaitTime);
            sample.GpuFrameTimeMs = Positive(timing.gpuFrameTime);
            sample.CpuFrameTimeMs = Positive(timing.cpuFrameTime);
        }

        // 两边换算成秒再比较，时钟源相同时只差几十微秒
        long FindByStart(long completed, double startSeconds)
        {
            long limit = System.Math.Max(0, completed - 16);
            for (long i = completed - 1; i >= limit; i--)
            {
                if (System.Math.Abs(frameStartTicks[i] * stopwatchSecondsPerTick - startSeconds) <= MatchToleranceSeconds)
                {
                    return i;
                }
            }
            return -1;
        }

        // 不支持的计时项 Unity 会返回 0，真实的帧耗时不可能恰好为 0
        static double Positive(double value)
        {
            return value > 0.0 ? value : double.NaN;
        }

        static double NonNegative(double value)
        {
            return value >= 0.0 ? value : double.NaN;
        }
    }
}
