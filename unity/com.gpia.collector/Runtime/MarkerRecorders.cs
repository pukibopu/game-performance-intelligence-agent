using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Unity.Profiling;
using Unity.Profiling.LowLevel.Unsafe;

namespace Gpia.Collector
{
    // 实验用：按名字记录内置 marker 的每帧耗时与调用次数，结果写成长表 markers.csv
    internal sealed class MarkerRecorders : IDisposable
    {
        readonly List<string> requested;
        readonly List<string> found = new List<string>();
        readonly List<ProfilerRecorder> recorders = new List<ProfilerRecorder>();
        readonly long[][] timeNs;
        readonly int[][] calls;

        public MarkerRecorders(IReadOnlyList<string> names, int capacity)
        {
            requested = new List<string>(names);
            var available = new List<ProfilerRecorderHandle>();
            ProfilerRecorderHandle.GetAvailable(available);
            foreach (string name in names)
            {
                foreach (ProfilerRecorderHandle handle in available)
                {
                    if (ProfilerRecorderHandle.GetDescription(handle).Name == name)
                    {
                        var recorder = new ProfilerRecorder(handle, 1, ProfilerRecorderOptions.Default);
                        recorder.Start();
                        recorders.Add(recorder);
                        found.Add(name);
                        break;
                    }
                }
            }
            timeNs = new long[recorders.Count][];
            calls = new int[recorders.Count][];
            for (int i = 0; i < recorders.Count; i++)
            {
                timeNs[i] = new long[capacity];
                calls[i] = new int[capacity];
                Array.Fill(timeNs[i], -1L);
            }
        }

        public bool Any => recorders.Count > 0 || requested.Count > 0;

        public void Record(long sampleIndex)
        {
            for (int i = 0; i < recorders.Count; i++)
            {
                ProfilerRecorder r = recorders[i];
                if (r.Valid && r.Count > 0)
                {
                    timeNs[i][sampleIndex] = r.LastValue;
                    calls[i][sampleIndex] = (int)Math.Min(r.GetSample(0).Count, int.MaxValue);
                }
            }
        }

        public void Write(string runDir, long samples)
        {
            var csv = new StringBuilder("frame_index,marker,time_ns,calls\n");
            for (long f = 0; f < samples; f++)
            {
                for (int i = 0; i < found.Count; i++)
                {
                    if (timeNs[i][f] >= 0)
                    {
                        csv.Append(f.ToString(CultureInfo.InvariantCulture)).Append(',').Append(found[i]).Append(',')
                            .Append(timeNs[i][f].ToString(CultureInfo.InvariantCulture)).Append(',')
                            .Append(calls[i][f].ToString(CultureInfo.InvariantCulture)).Append('\n');
                    }
                }
            }
            File.WriteAllText(Path.Combine(runDir, "markers.csv"), csv.ToString(), new UTF8Encoding(false));

            var meta = new StringBuilder("{\n  \"requested\": [");
            meta.Append(string.Join(", ", requested.ConvertAll(Json.Quote))).Append("],\n  \"found\": [");
            meta.Append(string.Join(", ", found.ConvertAll(Json.Quote))).Append("]\n}\n");
            File.WriteAllText(Path.Combine(runDir, "markers_meta.json"), meta.ToString(), new UTF8Encoding(false));
        }

        public void Dispose()
        {
            foreach (ProfilerRecorder r in recorders)
            {
                r.Dispose();
            }
        }
    }
}
