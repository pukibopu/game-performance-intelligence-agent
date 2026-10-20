using System;
using System.Diagnostics;
using Gpia.Collector;

namespace Gpia.Benchmark
{
    // 一个可复现的负载，每个测量帧调用一次
    public interface IScenario
    {
        void Tick(int frame);
    }

    public static class ScenarioCatalog
    {
        public const uint InjectedMarker = 1;

        public static IScenario Create(string name, RunOptions options)
        {
            switch (name)
            {
                case "idle":
                    return new IdleScenario();
                case "gc_pressure":
                    return new GcPressureScenario(options);
                case "cpu_spike":
                    return new CpuSpikeScenario(options);
                default:
                    throw new ArgumentException($"unknown scenario '{name}'");
            }
        }
    }

    // 不注入任何负载，用来测量采集器自身的开销
    public sealed class IdleScenario : IScenario
    {
        public void Tick(int frame)
        {
        }
    }

    // 每个周期分配一块临时托管内存，制造可控的 GC 压力
    public sealed class GcPressureScenario : IScenario
    {
        sealed class Node
        {
            public Node Next;
            public long Payload;
        }

        readonly int period;
        readonly int allocBytes;
        readonly int pieces;
        readonly Node liveHeap;
        readonly bool forceCollect;
        static object keepAlive;

        public GcPressureScenario(RunOptions options)
        {
            period = Math.Max(1, options.GetInt("period_frames", 120));
            allocBytes = Math.Max(1, options.GetInt("alloc_bytes", 8 * 1024 * 1024));
            pieces = Math.Max(1, options.GetInt("pieces", 1));
            forceCollect = options.GetInt("force_collect", 0) != 0;

            // 常驻的对象链让每次回收都要标记更多存活对象，回收耗时随之变长
            int retained = Math.Max(0, options.GetInt("retained_objects", 0));
            for (int i = 0; i < retained; i++)
            {
                liveHeap = new Node { Next = liveHeap, Payload = i };
            }
        }

        public long LiveObjects => liveHeap == null ? 0 : liveHeap.Payload + 1;

        public void Tick(int frame)
        {
            if (frame % period != period / 2)
            {
                return;
            }
            int size = Math.Max(1, allocBytes / pieces);
            for (int i = 0; i < pieces; i++)
            {
                // 写入首尾字节并短暂引用，防止 JIT 把分配优化掉
                var block = new byte[size];
                block[0] = 1;
                block[size - 1] = 1;
                keepAlive = block;
            }
            keepAlive = null;
            if (forceCollect)
            {
                // 增量 GC 会把回收摊到多帧，强制一次完整的阻塞式回收才能复现典型的 GC 卡顿
                GC.Collect();
            }
            GpiaSession.MarkFrame(ScenarioCatalog.InjectedMarker);
        }
    }

    // 每个周期让主线程忙等一段固定时间，制造可控的 CPU 尖峰
    public sealed class CpuSpikeScenario : IScenario
    {
        readonly int period;
        readonly double spikeMs;
        static double sink;

        public CpuSpikeScenario(RunOptions options)
        {
            period = Math.Max(1, options.GetInt("period_frames", 120));
            spikeMs = Math.Max(0.1, options.GetDouble("spike_ms", 30.0));
        }

        public void Tick(int frame)
        {
            if (frame % period != period / 2)
            {
                return;
            }
            long start = Stopwatch.GetTimestamp();
            long budget = (long)(spikeMs * Stopwatch.Frequency / 1000.0);
            double acc = 0.0;
            int i = 0;
            while (Stopwatch.GetTimestamp() - start < budget)
            {
                acc += Math.Sqrt(++i);
            }
            sink = acc;
            GpiaSession.MarkFrame(ScenarioCatalog.InjectedMarker);
        }
    }
}
