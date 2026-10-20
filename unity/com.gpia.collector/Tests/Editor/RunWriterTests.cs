using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Threading;
using NUnit.Framework;

namespace Gpia.Collector.Tests
{
    public class RunWriterTests
    {
        static FrameSample[] TwoFrames()
        {
            FrameSample a = FrameSample.Empty(0);
            a.TimestampNs = 16_683_000;
            a.FrameTimeMs = 16.683;
            a.CpuMainThreadMs = 12.41;
            a.GcUsedBytes = 52_428_800;
            a.GcCollectionCount = 12;
            FrameSample b = FrameSample.Empty(1);
            b.TimestampNs = 33_367_000;
            b.FrameTimeMs = 16.684;
            b.GcUsedBytes = 52_428_800;
            b.GcCollectionCount = 12;
            b.ScenarioMarker = 1;
            return new[] { a, b };
        }

        [Test]
        public void WritesHeaderAndEmptyCellsForMissingValues()
        {
            FrameSample[] samples = TwoFrames();
            bool[] present = RunWriter.DetectPresentColumns(samples, samples.Length);
            string[] lines = Encoding.UTF8.GetString(RunWriter.FormatFrames(samples, samples.Length, present)).Split('\n');

            Assert.AreEqual(string.Join(",", RunWriter.ColumnNames()), lines[0]);
            Assert.AreEqual("0,16683000,16.683,12.410,,,,,52428800,,12,,,,,,,,,,,,0", lines[1]);
            Assert.AreEqual("1,33367000,16.684,,,,,,52428800,,12,,,,,,,,,,,,1", lines[2]);
            Assert.AreEqual("", lines[3]); // 文件以换行结尾
        }

        [Test]
        public void UsesInvariantDecimalPointUnderAnyCulture()
        {
            CultureInfo previous = Thread.CurrentThread.CurrentCulture;
            try
            {
                Thread.CurrentThread.CurrentCulture = new CultureInfo("de-DE");
                FrameSample[] samples = TwoFrames();
                bool[] present = RunWriter.DetectPresentColumns(samples, samples.Length);
                string text = Encoding.UTF8.GetString(RunWriter.FormatFrames(samples, samples.Length, present));
                StringAssert.Contains("16.683", text);
                StringAssert.DoesNotContain("16,683", text);
            }
            finally
            {
                Thread.CurrentThread.CurrentCulture = previous;
            }
        }

        [Test]
        public void DeclaresEmptyColumnsUnsupported()
        {
            FrameSample[] samples = TwoFrames();
            bool[] present = RunWriter.DetectPresentColumns(samples, samples.Length);
            var manifest = new ManifestData
            {
                RunId = "test-run",
                FrameCount = 2,
                FramesSha256 = new string('0', 64),
                Env = new EnvironmentInfo
                {
                    UnityVersion = "2022.3.62f3", BuildId = "b", ScriptingBackend = "mono", GraphicsApi = "Direct3D11",
                    RenderPipeline = "urp", Os = "os", Cpu = "cpu", CpuLogicalCores = 1, SystemMemoryMb = 1,
                    GpuName = "gpu", Width = 1, Height = 1, FullscreenMode = "Windowed", QualityLevel = "High",
                },
                ScenarioName = "unit_test",
                ScenarioParams = new SortedDictionary<string, string> { { "period_frames", "120" }, { "label", "x" } },
                ColumnSources = new string[RunWriter.ColumnCount],
            };
            for (int i = 0; i < manifest.ColumnSources.Length; i++)
            {
                manifest.ColumnSources[i] = "test";
            }
            string json = RunWriter.FormatManifest(manifest, present);

            StringAssert.Contains("{\"name\": \"gc_alloc_bytes\", \"unit\": \"bytes\", \"availability\": \"unsupported\"", json);
            StringAssert.Contains("{\"name\": \"gc_used_bytes\", \"unit\": \"bytes\", \"availability\": \"measured\"", json);
            StringAssert.Contains("\"params\": {\"label\": \"x\", \"period_frames\": 120}", json);
            StringAssert.Contains("\"abort_reason\": null", json);
        }

        [Test]
        public void Sha256MatchesKnownVector()
        {
            Assert.AreEqual("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                RunWriter.Sha256Hex(Encoding.ASCII.GetBytes("abc")));
        }

        [Test]
        public void JsonQuoteEscapesControlCharacters()
        {
            Assert.AreEqual("\"a\\\"b\\\\c\\n\\u0001\"", Json.Quote("a\"b\\c\n\u0001"));
            Assert.AreEqual("null", Json.Quote(null));
        }
    }
}
