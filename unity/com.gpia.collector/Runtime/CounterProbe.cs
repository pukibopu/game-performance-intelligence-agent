using System.Collections.Generic;
using System.IO;
using System.Text;
using Unity.Profiling.LowLevel.Unsafe;

namespace Gpia.Collector
{
    // 实验 U1：列出当前构建里 ProfilerRecorder 能找到的全部计数器和 marker
    public static class CounterProbe
    {
        public static int Write(string path)
        {
            var handles = new List<ProfilerRecorderHandle>();
            ProfilerRecorderHandle.GetAvailable(handles);
            var rows = new List<string>(handles.Count);
            foreach (ProfilerRecorderHandle handle in handles)
            {
                ProfilerRecorderDescription d = ProfilerRecorderHandle.GetDescription(handle);
                rows.Add(string.Join(",", Csv.Escape(d.Name), Csv.Escape(d.Category.Name), d.UnitType.ToString(),
                    d.DataType.ToString(), d.Flags.ToString().Replace(", ", "|")));
            }
            rows.Sort(System.StringComparer.Ordinal);

            string directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }
            var text = new StringBuilder("name,category,unit,data_type,flags\n");
            foreach (string row in rows)
            {
                text.Append(row).Append('\n');
            }
            File.WriteAllText(path, text.ToString(), new UTF8Encoding(false));
            return rows.Count;
        }
    }
}
