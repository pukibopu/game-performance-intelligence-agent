using System;
using Unity.Profiling;

namespace Gpia.Collector
{
    // 每个计数器一个 ProfilerRecorder，只读上一帧已经结算好的值
    internal sealed class MetricRecorders : IDisposable
    {
        ProfilerRecorder gcAlloc;
        ProfilerRecorder gcUsed;
        ProfilerRecorder gcReserved;
        ProfilerRecorder totalUsed;
        ProfilerRecorder totalReserved;
        ProfilerRecorder systemUsed;
        ProfilerRecorder drawCalls;
        ProfilerRecorder setPassCalls;
        ProfilerRecorder batches;
        ProfilerRecorder triangles;
        ProfilerRecorder vertices;
        ProfilerRecorder gcCollect;

        public MetricRecorders()
        {
            gcAlloc = ProfilerRecorder.StartNew(ProfilerCategory.Memory, "GC Allocated In Frame");
            gcUsed = ProfilerRecorder.StartNew(ProfilerCategory.Memory, "GC Used Memory");
            gcReserved = ProfilerRecorder.StartNew(ProfilerCategory.Memory, "GC Reserved Memory");
            totalUsed = ProfilerRecorder.StartNew(ProfilerCategory.Memory, "Total Used Memory");
            totalReserved = ProfilerRecorder.StartNew(ProfilerCategory.Memory, "Total Reserved Memory");
            systemUsed = ProfilerRecorder.StartNew(ProfilerCategory.Memory, "System Used Memory");
            drawCalls = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Draw Calls Count");
            setPassCalls = ProfilerRecorder.StartNew(ProfilerCategory.Render, "SetPass Calls Count");
            // 文档表格写的是 Total Batches Count，实测 2022.3 Player 里只有 Batches Count
            batches = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Batches Count");
            triangles = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Triangles Count");
            vertices = ProfilerRecorder.StartNew(ProfilerCategory.Render, "Vertices Count");
            // GC.Collect 是 marker 而不是计数器，实测 Release Player 也能记录它每帧的耗时
            gcCollect = ProfilerRecorder.StartNew(new ProfilerCategory("GC"), "GC.Collect");
        }

        public void Fill(ref FrameSample sample)
        {
            sample.GcAllocBytes = Read(ref gcAlloc);
            sample.GcUsedBytes = Read(ref gcUsed);
            sample.GcReservedBytes = Read(ref gcReserved);
            sample.TotalUsedBytes = Read(ref totalUsed);
            sample.TotalReservedBytes = Read(ref totalReserved);
            sample.SystemUsedBytes = Read(ref systemUsed);
            sample.DrawCalls = ReadInt(ref drawCalls);
            sample.SetPassCalls = ReadInt(ref setPassCalls);
            sample.Batches = ReadInt(ref batches);
            sample.Triangles = Read(ref triangles);
            sample.Vertices = Read(ref vertices);
            long gcNs = Read(ref gcCollect);
            sample.GcCollectMs = gcNs < 0 ? double.NaN : gcNs / 1e6;
        }

        public void Dispose()
        {
            gcAlloc.Dispose();
            gcUsed.Dispose();
            gcReserved.Dispose();
            totalUsed.Dispose();
            totalReserved.Dispose();
            systemUsed.Dispose();
            drawCalls.Dispose();
            setPassCalls.Dispose();
            batches.Dispose();
            triangles.Dispose();
            vertices.Dispose();
            gcCollect.Dispose();
        }

        // 记录器无效或从未收到样本时视为缺失，绝不用 0 代替
        static long Read(ref ProfilerRecorder recorder)
        {
            return recorder.Valid && recorder.Count > 0 ? recorder.LastValue : FrameSample.MissingLong;
        }

        static int ReadInt(ref ProfilerRecorder recorder)
        {
            long value = Read(ref recorder);
            return value < 0 ? FrameSample.MissingInt : (int)Math.Min(value, int.MaxValue);
        }
    }
}
