# /// script
# requires-python = ">=3.12"
# dependencies = ["jsonschema>=4.23", "rfc3339-validator>=0.1.4"]
# ///
"""Validate a JSON document against a JSON Schema (Draft 2020-12)."""

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: validate_schema.py <schema.json> <instance.json>", file=sys.stderr)
        return 2

    schema = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    instance = json.loads(Path(argv[2]).read_text(encoding="utf-8"))

    Draft202012Validator.check_schema(schema)  # the schema itself must be valid first
    validator = Draft202012Validator(schema, format_checker=Draft202012Validator.FORMAT_CHECKER)

    errors = sorted(validator.iter_errors(instance), key=lambda e: list(e.absolute_path))
    for err in errors:
        location = "/".join(str(p) for p in err.absolute_path) or "<root>"
        print(f"ERROR at {location}: {err.message}")

    if errors:
        return 1
    print(f"OK: {argv[2]} is valid against {argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
