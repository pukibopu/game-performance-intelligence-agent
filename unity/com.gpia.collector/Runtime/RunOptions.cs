using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace Gpia.Collector
{
    // 从命令行读取一次采集的全部参数，所有参数都以 -gpia- 开头
    public sealed class RunOptions
    {
        public string Scenario;
        public double WarmupSeconds = 5.0;
        public double DurationSeconds = 30.0;
        public string OutDir = DefaultOutDir();
        public int Seed = 42;
        public string RunId;
        public int TargetFrameRate = 60;
        public int VSyncCount;
        public int MaxFrameRate = 1000;
        // 在帧开始取样时实测为 5 帧，比文档写的 4 帧多一帧
        public int FrameTimingLag = 5;
        public string ProbeCountersPath;
        public readonly List<string> Markers = new List<string>();
        public readonly SortedDictionary<string, string> Params = new SortedDictionary<string, string>(StringComparer.Ordinal);

        public static RunOptions FromCommandLine()
        {
            return Parse(Environment.GetCommandLineArgs());
        }

        public static RunOptions Parse(IReadOnlyList<string> args)
        {
            var options = new RunOptions();
            for (int i = 0; i < args.Count; i++)
            {
                string name = args[i];
                if (!name.StartsWith("-gpia-", StringComparison.Ordinal))
                {
                    continue;
                }
                if (i + 1 >= args.Count)
                {
                    throw new ArgumentException($"{name} needs a value");
                }
                string value = args[++i];
                switch (name)
                {
                    case "-gpia-scenario":
                        options.Scenario = value;
                        break;
                    case "-gpia-warmup":
                        options.WarmupSeconds = ParseDouble(name, value, 0.0);
                        break;
                    case "-gpia-duration":
                        options.DurationSeconds = ParseDouble(name, value, 0.1);
                        break;
                    case "-gpia-out":
                        options.OutDir = value;
                        break;
                    case "-gpia-seed":
                        options.Seed = ParseInt(name, value, int.MinValue);
                        break;
                    case "-gpia-run-id":
                        options.RunId = value;
                        break;
                    case "-gpia-target-fps":
                        options.TargetFrameRate = ParseInt(name, value, -1);
                        break;
                    case "-gpia-vsync":
                        options.VSyncCount = ParseInt(name, value, 0);
                        break;
                    case "-gpia-max-fps":
                        options.MaxFrameRate = ParseInt(name, value, 1);
                        break;
                    case "-gpia-frametiming-lag":
                        options.FrameTimingLag = ParseInt(name, value, 0);
                        break;
                    case "-gpia-probe-counters":
                        options.ProbeCountersPath = value;
                        break;
                    case "-gpia-markers":
                        options.Markers.AddRange(value.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries));
                        break;
                    case "-gpia-param":
                        int eq = value.IndexOf('=');
                        if (eq <= 0)
                        {
                            throw new ArgumentException($"-gpia-param expects key=value, got '{value}'");
                        }
                        options.Params[value.Substring(0, eq)] = value.Substring(eq + 1);
                        break;
                    default:
                        throw new ArgumentException($"unknown option {name}");
                }
            }
            return options;
        }

        // 场景参数缺省时用默认值，格式错误时直接报错而不是悄悄回退
        public int GetInt(string key, int fallback)
        {
            return Params.TryGetValue(key, out string text) ? ParseInt(key, text, int.MinValue) : fallback;
        }

        public double GetDouble(string key, double fallback)
        {
            return Params.TryGetValue(key, out string text) ? ParseDouble(key, text, double.MinValue) : fallback;
        }

        static string DefaultOutDir()
        {
            string home = Environment.GetEnvironmentVariable("GPIA_HOME");
            if (string.IsNullOrEmpty(home))
            {
                home = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "gpia");
            }
            return Path.Combine(home, "runs");
        }

        static int ParseInt(string name, string text, int min)
        {
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) || value < min)
            {
                throw new ArgumentException($"{name} expects an integer of at least {min}, got '{text}'");
            }
            return value;
        }

        static double ParseDouble(string name, string text, double min)
        {
            if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double value) ||
                double.IsNaN(value) || double.IsInfinity(value) || value < min)
            {
                throw new ArgumentException($"{name} expects a number of at least {min}, got '{text}'");
            }
            return value;
        }
    }
}
