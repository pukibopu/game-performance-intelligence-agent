using System;
using NUnit.Framework;

namespace Gpia.Collector.Tests
{
    public class RunOptionsTests
    {
        [Test]
        public void ParsesKnownOptions()
        {
            RunOptions o = RunOptions.Parse(new[]
            {
                "GpiaBenchmark.exe", "-gpia-scenario", "gc_pressure", "-gpia-duration", "12.5", "-gpia-warmup", "3",
                "-gpia-out", "C:/runs", "-gpia-seed", "7", "-gpia-target-fps", "-1", "-gpia-param", "alloc_bytes=1024",
                "-gpia-markers", "GC.Collect,PlayerLoop",
            });

            Assert.AreEqual("gc_pressure", o.Scenario);
            Assert.AreEqual(12.5, o.DurationSeconds);
            Assert.AreEqual(3.0, o.WarmupSeconds);
            Assert.AreEqual("C:/runs", o.OutDir);
            Assert.AreEqual(7, o.Seed);
            Assert.AreEqual(-1, o.TargetFrameRate);
            Assert.AreEqual(1024, o.GetInt("alloc_bytes", 0));
            CollectionAssert.AreEqual(new[] { "GC.Collect", "PlayerLoop" }, o.Markers);
        }

        [Test]
        public void IgnoresArgumentsThatAreNotOurs()
        {
            RunOptions o = RunOptions.Parse(new[] { "Unity.exe", "-batchmode", "-logFile", "x.log" });
            Assert.IsNull(o.Scenario);
        }

        [Test]
        public void RejectsUnknownOrMalformedOptions()
        {
            Assert.Throws<ArgumentException>(() => RunOptions.Parse(new[] { "-gpia-bogus", "1" }));
            Assert.Throws<ArgumentException>(() => RunOptions.Parse(new[] { "-gpia-duration", "abc" }));
            Assert.Throws<ArgumentException>(() => RunOptions.Parse(new[] { "-gpia-param", "noequals" }));
            Assert.Throws<ArgumentException>(() => RunOptions.Parse(new[] { "-gpia-scenario" }));
        }

        [Test]
        public void MalformedScenarioParameterIsAnError()
        {
            RunOptions o = RunOptions.Parse(new[] { "-gpia-param", "period_frames=often" });
            Assert.Throws<ArgumentException>(() => o.GetInt("period_frames", 120));
            Assert.AreEqual(120, RunOptions.Parse(new string[0]).GetInt("period_frames", 120));
        }
    }
}
