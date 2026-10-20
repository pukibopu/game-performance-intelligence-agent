using UnityEngine;

namespace Gpia.Benchmark
{
    // 让相机匀速绕场景旋转，保持稳定且非静止的渲染负载
    public sealed class CameraOrbit : MonoBehaviour
    {
        public float degreesPerSecond = 20f;
        public float radius = 16f;
        public float height = 7f;

        void LateUpdate()
        {
            float angle = Time.time * degreesPerSecond * Mathf.Deg2Rad;
            transform.position = new Vector3(Mathf.Sin(angle) * radius, height, Mathf.Cos(angle) * radius);
            transform.LookAt(Vector3.zero);
        }
    }
}
