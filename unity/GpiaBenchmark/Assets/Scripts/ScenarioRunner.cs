using System;
using Gpia.Collector;
using UnityEngine;

namespace Gpia.Benchmark
{
    // 驱动一次采集：先预热，再在测量期内注入场景负载，结束后写出 run 目录并退出
    public sealed class ScenarioRunner : MonoBehaviour
    {
        RunOptions options;
        IScenario scenario;
        double startTime;
        bool measuring;
        bool finished;
        int measuredFrames;

        public void Configure(RunOptions runOptions)
        {
            options = runOptions;
        }

        void Start()
        {
            QualitySettings.vSyncCount = options.VSyncCount;
            Application.targetFrameRate = options.TargetFrameRate;
            Application.runInBackground = true;
            try
            {
                scenario = ScenarioCatalog.Create(options.Scenario, options);
            }
            catch (ArgumentException e)
            {
                Debug.LogError("[GPIA] " + e.Message);
                finished = true;
                Application.Quit(2);
                return;
            }
            GpiaSession.Begin(options, options.Scenario);
            startTime = Time.realtimeSinceStartupAsDouble;
        }

        void Update()
        {
            if (finished)
            {
                return;
            }
            double elapsed = Time.realtimeSinceStartupAsDouble - startTime;
            if (!measuring && elapsed >= options.WarmupSeconds)
            {
                GpiaSession.EndWarmup();
                measuring = true;
            }
            if (measuring)
            {
                try
                {
                    scenario.Tick(measuredFrames++);
                }
                catch (Exception e)
                {
                    Finish(true, $"scenario threw {e.GetType().Name}: {e.Message}", true);
                    return;
                }
            }
            if (elapsed >= options.WarmupSeconds + options.DurationSeconds)
            {
                Finish(false, null, true);
            }
        }

        void OnApplicationQuit()
        {
            if (!finished && GpiaSession.IsRecording)
            {
                Finish(true, "the application quit before the run finished", false);
            }
        }

        void Finish(bool aborted, string reason, bool quit)
        {
            finished = true;
            string runDir = GpiaSession.End(aborted, reason);
            Debug.Log("[GPIA] run directory: " + runDir);
            if (quit)
            {
                Application.Quit(aborted ? 1 : 0);
            }
        }
    }
}
