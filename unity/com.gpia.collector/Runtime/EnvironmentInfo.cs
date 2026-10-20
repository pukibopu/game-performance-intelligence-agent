using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Scripting;

namespace Gpia.Collector
{
    // 采集开始时的构建、设备与显示环境，写进 manifest 用于判断两次运行是否可比
    internal sealed class EnvironmentInfo
    {
        public string UnityVersion;
        public string BuildId;
        public bool DevelopmentBuild;
        public string ScriptingBackend;
        public string GraphicsApi;
        public string RenderPipeline;
        public bool FrameTimingStats;
        public bool IncrementalGc;
        public bool IsEditor;

        public string Os;
        public string Cpu;
        public int CpuLogicalCores;
        public int SystemMemoryMb;
        public string GpuName;
        public int GpuMemoryMb;

        public int Width;
        public int Height;
        public string FullscreenMode;
        public int VSyncCount;
        public int TargetFrameRate;
        public string QualityLevel;

        public static EnvironmentInfo Capture()
        {
            string buildId = Application.buildGUID;
            return new EnvironmentInfo
            {
                UnityVersion = Application.unityVersion,
                BuildId = string.IsNullOrEmpty(buildId) ? "editor" : buildId,
                DevelopmentBuild = Debug.isDebugBuild,
#if ENABLE_IL2CPP
                ScriptingBackend = "il2cpp",
#else
                ScriptingBackend = "mono",
#endif
                GraphicsApi = SystemInfo.graphicsDeviceType.ToString(),
                RenderPipeline = DescribePipeline(GraphicsSettings.currentRenderPipeline),
                FrameTimingStats = FrameTimingManager.IsFeatureEnabled(),
                IncrementalGc = GarbageCollector.isIncremental,
                IsEditor = Application.isEditor,
                Os = SystemInfo.operatingSystem,
                Cpu = SystemInfo.processorType,
                CpuLogicalCores = SystemInfo.processorCount,
                SystemMemoryMb = SystemInfo.systemMemorySize,
                GpuName = SystemInfo.graphicsDeviceName,
                GpuMemoryMb = SystemInfo.graphicsMemorySize,
                Width = Screen.width,
                Height = Screen.height,
                FullscreenMode = Screen.fullScreenMode.ToString(),
                VSyncCount = QualitySettings.vSyncCount,
                TargetFrameRate = Application.targetFrameRate,
                QualityLevel = QualitySettings.names[QualitySettings.GetQualityLevel()],
            };
        }

        static string DescribePipeline(RenderPipelineAsset asset)
        {
            if (asset == null)
            {
                return "builtin";
            }
            string type = asset.GetType().Name;
            if (type.Contains("Universal"))
            {
                return "urp";
            }
            return type.Contains("HDRenderPipeline") ? "hdrp" : "custom";
        }
    }
}
