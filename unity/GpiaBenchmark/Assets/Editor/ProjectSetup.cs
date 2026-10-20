using UnityEditor;
using UnityEditor.Build;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.SceneManagement;

namespace Gpia.Benchmark.Editor
{
    // 用脚本而不是手工点选来配置工程，保证任何人在 batchmode 下都能得到同样的设置
    public static class ProjectSetup
    {
        public const string ScenePath = "Assets/Scenes/Benchmark.unity";
        const string PipelinePath = "Assets/Settings/GpiaURP.asset";
        const string RendererPath = "Assets/Settings/GpiaURP_Renderer.asset";
        const string PostProcessPath = "Packages/com.unity.render-pipelines.universal/Runtime/Data/PostProcessData.asset";

        [MenuItem("GPIA/Configure Benchmark Project")]
        public static void Run()
        {
            EnsureFolder("Assets", "Settings");
            EnsureFolder("Assets", "Scenes");

            UniversalRenderPipelineAsset pipeline = LoadOrCreatePipeline();
            GraphicsSettings.defaultRenderPipeline = pipeline;
            for (int i = 0; i < QualitySettings.names.Length; i++)
            {
                QualitySettings.SetQualityLevel(i, false);
                QualitySettings.renderPipeline = null;
                QualitySettings.vSyncCount = 0;
            }

            ConfigurePlayer();
            BuildScene(pipeline);
            EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(ScenePath, true) };
            AssetDatabase.SaveAssets();
            Debug.Log("[GPIA] benchmark project configured");
        }

        static void ConfigurePlayer()
        {
            PlayerSettings.companyName = "GPIA";
            PlayerSettings.productName = "GpiaBenchmark";
            PlayerSettings.fullScreenMode = FullScreenMode.Windowed;
            PlayerSettings.defaultScreenWidth = 1280;
            PlayerSettings.defaultScreenHeight = 720;
            PlayerSettings.resizableWindow = false;
            PlayerSettings.runInBackground = true;
            PlayerSettings.visibleInBackground = true;
            PlayerSettings.enableFrameTimingStats = true;
            PlayerSettings.colorSpace = ColorSpace.Linear;
            PlayerSettings.gcIncremental = true;
            PlayerSettings.usePlayerLog = true;
            PlayerSettings.SplashScreen.show = false;
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.StandaloneWindows64, false);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.StandaloneWindows64, new[] { GraphicsDeviceType.Direct3D11 });
            PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.Mono2x);
        }

        static UniversalRenderPipelineAsset LoadOrCreatePipeline()
        {
            var existing = AssetDatabase.LoadAssetAtPath<UniversalRenderPipelineAsset>(PipelinePath);
            if (existing != null)
            {
                return existing;
            }
            var renderer = ScriptableObject.CreateInstance<UniversalRendererData>();
            renderer.postProcessData = AssetDatabase.LoadAssetAtPath<PostProcessData>(PostProcessPath);
            AssetDatabase.CreateAsset(renderer, RendererPath);
            ResourceReloader.ReloadAllNullIn(renderer, UniversalRenderPipelineAsset.packagePath);

            UniversalRenderPipelineAsset pipeline = UniversalRenderPipelineAsset.Create(renderer);
            AssetDatabase.CreateAsset(pipeline, PipelinePath);
            return pipeline;
        }

        // 场景里放一个绕行相机、一盏方向光和一百个立方体，作为所有场景共享的稳定渲染负载
        static void BuildScene(UniversalRenderPipelineAsset pipeline)
        {
            Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            var cameraObject = new GameObject("Main Camera") { tag = "MainCamera" };
            cameraObject.AddComponent<Camera>();
            cameraObject.AddComponent<UniversalAdditionalCameraData>();
            cameraObject.AddComponent<CameraOrbit>();

            var lightObject = new GameObject("Directional Light");
            var light = lightObject.AddComponent<Light>();
            light.type = LightType.Directional;
            lightObject.AddComponent<UniversalAdditionalLightData>();
            lightObject.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            Material material = pipeline.defaultMaterial;
            var root = new GameObject("Cubes");
            for (int x = 0; x < 10; x++)
            {
                for (int z = 0; z < 10; z++)
                {
                    GameObject cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
                    Object.DestroyImmediate(cube.GetComponent<Collider>());
                    cube.transform.SetParent(root.transform);
                    cube.transform.position = new Vector3((x - 4.5f) * 1.6f, 0f, (z - 4.5f) * 1.6f);
                    cube.GetComponent<MeshRenderer>().sharedMaterial = material;
                }
            }
            EditorSceneManager.SaveScene(scene, ScenePath);
        }

        static void EnsureFolder(string parent, string name)
        {
            if (!AssetDatabase.IsValidFolder(parent + "/" + name))
            {
                AssetDatabase.CreateFolder(parent, name);
            }
        }
    }
}
