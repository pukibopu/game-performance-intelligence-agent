namespace Gpia.Collector
{
    // 一帧的原始采样，NaN 和负数只在内存里表示缺失，写出时一律变成空单元格
    internal struct FrameSample
    {
        public const long MissingLong = -1;
        public const int MissingInt = -1;

        public long FrameIndex;
        public long TimestampNs;
        public double FrameTimeMs;
        public double CpuMainThreadMs;
        public double CpuRenderThreadMs;
        public double CpuPresentWaitMs;
        public double GpuFrameTimeMs;
        public long GcAllocBytes;
        public long GcUsedBytes;
        public long GcReservedBytes;
        public int GcCollectionCount;
        public long TotalUsedBytes;
        public long TotalReservedBytes;
        public long SystemUsedBytes;
        public int DrawCalls;
        public int SetPassCalls;
        public int Batches;
        public long Triangles;
        public long Vertices;
        public int UnityFrameCount;
        public double CpuFrameTimeMs;
        public double GcCollectMs;
        public uint ScenarioMarker;

        public static FrameSample Empty(long frameIndex)
        {
            return new FrameSample
            {
                FrameIndex = frameIndex,
                CpuMainThreadMs = double.NaN,
                CpuRenderThreadMs = double.NaN,
                CpuPresentWaitMs = double.NaN,
                GpuFrameTimeMs = double.NaN,
                GcAllocBytes = MissingLong,
                GcUsedBytes = MissingLong,
                GcReservedBytes = MissingLong,
                GcCollectionCount = MissingInt,
                TotalUsedBytes = MissingLong,
                TotalReservedBytes = MissingLong,
                SystemUsedBytes = MissingLong,
                DrawCalls = MissingInt,
                SetPassCalls = MissingInt,
                Batches = MissingInt,
                Triangles = MissingLong,
                Vertices = MissingLong,
                UnityFrameCount = MissingInt,
                CpuFrameTimeMs = double.NaN,
                GcCollectMs = double.NaN,
            };
        }
    }
}
