# /// script
# requires-python = ">=3.12"
# dependencies = ["jsonschema>=4.23", "rfc3339-validator>=0.1.4"]
# ///
"""端到端检查 gpia-core 对每个合成用例的退出码、schema 合规性与 ground truth"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator

EXIT_OK = 0
EXIT_INVALID_INPUT = 4
EXIT_INSUFFICIENT_DATA = 5


def frames_of(report: dict, kind: str) -> list[int]:
    frames: list[int] = []
    for finding in report["findings"]:
        if finding["kind"] == kind and "frame_range" in finding:
            r = finding["frame_range"]
            frames.extend(range(r["first"], r["last"] + 1))
    return sorted(frames)


def check_case(
    cli: Path, case_dir: Path, validator: Draft202012Validator, out_dir: Path
) -> list[str]:
    expected = json.loads((case_dir / "expected.json").read_text(encoding="utf-8"))
    name = expected["case"]
    out_file = out_dir / f"{name}.json"
    md_file = out_dir / f"{name}.md"
    proc = subprocess.run(
        [
            str(cli),
            "analyze",
            "--run",
            str(case_dir),
            "--out",
            str(out_file),
            "--md-out",
            str(md_file),
        ],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    errors: list[str] = []

    # stderr 每一行都必须是独立的 JSON 诊断
    for line in proc.stderr.splitlines():
        try:
            json.loads(line)
        except json.JSONDecodeError:
            errors.append(f"{name}: stderr line is not JSON: {line[:80]}")

    if expected["fatal_error"] is not None:
        if proc.returncode != EXIT_INVALID_INPUT:
            errors.append(f"{name}: expected exit {EXIT_INVALID_INPUT}, got {proc.returncode}")
        return errors

    want_exit = EXIT_INSUFFICIENT_DATA if expected["analyzed_frames"] < 30 else EXIT_OK
    if proc.returncode != want_exit:
        return [*errors, f"{name}: expected exit {want_exit}, got {proc.returncode}"]

    report = json.loads(out_file.read_text(encoding="utf-8"))
    for err in validator.iter_errors(report):
        location = "/".join(str(p) for p in err.absolute_path) or "<root>"
        errors.append(f"{name}: schema error at {location}: {err.message}")

    evidence_ids = {e["id"] for e in report["evidence"]}
    for finding in report["findings"]:
        for ev in finding["evidence_ids"]:
            if ev not in evidence_ids:
                errors.append(f"{name}: {finding['id']} cites missing {ev}")

    truth = expected["expected"]
    checks = [
        ("frame_hitch", truth["hitch_frames"]),
        ("gc_alloc_spike", truth["gc_alloc_spike_frames"]),
    ]
    for kind, frames in checks:
        if frames_of(report, kind) != frames:
            errors.append(f"{name}: {kind} frames {frames_of(report, kind)} != {frames}")
    grew = bool(frames_of(report, "memory_growth"))
    if grew != truth["memory_growth"]:
        errors.append(f"{name}: memory_growth {grew} != {truth['memory_growth']}")
    if not md_file.read_text(encoding="utf-8").startswith("# GPIA analysis report"):
        errors.append(f"{name}: Markdown summary is missing its title")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="End-to-end check of gpia-core reports.")
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--testdata", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    args = parser.parse_args()

    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema, format_checker=Draft202012Validator.FORMAT_CHECKER)

    errors: list[str] = []
    cases = sorted(p for p in args.testdata.iterdir() if p.is_dir())
    with tempfile.TemporaryDirectory(prefix="gpia-reports-") as tmp:
        for case_dir in cases:
            errors.extend(check_case(args.cli, case_dir, validator, Path(tmp)))

    for err in errors:
        print(err)
    print(f"checked {len(cases)} cases, {len(errors)} problems")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
