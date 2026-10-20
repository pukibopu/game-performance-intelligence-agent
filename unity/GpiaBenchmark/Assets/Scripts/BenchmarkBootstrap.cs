using System;
using Gpia.Collector;
using UnityEngine;

namespace Gpia.Benchmark
{
    // 场景加载后按命令行参数决定做什么，Player 没有收到场景参数时直接以用法错误退出
    public static class BenchmarkBootstrap
    {
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void Boot()
        {
            RunOptions options;
            try
            {
                options = RunOptions.FromCommandLine();
            }
            catch (ArgumentException e)
            {
                Debug.LogError("[GPIA] " + e.Message);
                Application.Quit(2);
                return;
            }

            if (options.ProbeCountersPath != null)
            {
                int count = CounterProbe.Write(options.ProbeCountersPath);
                Debug.Log($"[GPIA] wrote {count} available counters to {options.ProbeCountersPath}");
                if (options.Scenario == null)
                {
                    Application.Quit(0);
                    return;
                }
            }

            if (options.Scenario == null)
            {
                if (!Application.isEditor)
                {
                    Debug.LogError("[GPIA] missing -gpia-scenario");
                    Application.Quit(2);
                }
                return;
            }

            var runner = new GameObject("GpiaScenarioRunner").AddComponent<ScenarioRunner>();
            runner.Configure(options);
        }
    }
}
