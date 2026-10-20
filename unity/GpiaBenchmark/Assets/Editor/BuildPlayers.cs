using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Gpia.Benchmark.Editor
{
    // 按 后端-档位 组合构建 Windows Player，失败时抛异常让 batchmode 返回非零退出码
    public static class BuildPlayers
    {
        public const string AllVariants = "mono-release,mono-dev,il2cpp-release,il2cpp-dev";

        public static void BuildFromCommandLine()
        {
            string[] args = Environment.GetCommandLineArgs();
            string variants = Value(args, "-gpia-builds") ?? AllVariants;
            string outRoot = Value(args, "-gpia-build-out") ?? "Builds";
            foreach (string variant in variants.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries))
            {
                Build(variant.Trim(), outRoot);
            }
        }

        [MenuItem("GPIA/Build All Players")]
        static void BuildAllFromMenu()
        {
            foreach (string variant in AllVariants.Split(','))
            {
                Build(variant, "Builds");
            }
        }

        public static void Build(string variant, string outRoot)
        {
            string[] parts = variant.Split('-');
            if (parts.Length != 2 || (parts[0] != "mono" && parts[0] != "il2cpp") ||
                (parts[1] != "release" && parts[1] != "dev"))
            {
                throw new ArgumentException($"unknown build variant '{variant}'");
            }
            ScriptingImplementation backend = parts[0] == "il2cpp" ? ScriptingImplementation.IL2CPP : ScriptingImplementation.Mono2x;
            bool development = parts[1] == "dev";

            PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, backend);
            try
            {
                var options = new BuildPlayerOptions
                {
                    scenes = new[] { ProjectSetup.ScenePath },
                    locationPathName = Path.Combine(outRoot, variant, "GpiaBenchmark.exe"),
                    target = BuildTarget.StandaloneWindows64,
                    targetGroup = BuildTargetGroup.Standalone,
                    options = development ? BuildOptions.Development : BuildOptions.None,
                };
                BuildReport report = BuildPipeline.BuildPlayer(options);
                if (report.summary.result != BuildResult.Succeeded)
                {
                    throw new Exception($"build {variant} finished with {report.summary.result}");
                }
                Debug.Log($"[GPIA] built {variant} in {report.summary.totalTime.TotalSeconds:F0} s");
            }
            finally
            {
                PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.Mono2x);
            }
        }

        static string Value(string[] args, string name)
        {
            int i = Array.IndexOf(args, name);
            return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        }
    }
}
