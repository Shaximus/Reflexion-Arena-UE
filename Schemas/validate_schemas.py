#!/usr/bin/env python3
"""
validate_schemas.py — Rx Worlds schema layer validator.

Checks, in order:
  1. Both schema files are well-formed JSON and (if the `jsonschema` package is
     available) are themselves valid JSON Schema draft 2020-12 documents.
  2. examples/mythology.universe.json PASSES validation against
     rx.universe-manifest.v1.schema.json (plus a strict json.load parse either way).
  3. An inline valid provenance chain PASSES validation against
     rx.provenance-chain.v1.schema.json (shape-level; chain continuity is verified
     procedurally in the receipt verifier — see README.md).
  4. examples/invalid_missing_comma.universe.json FAILS with a precise parse error
     (line/column reported). If it parses cleanly, that expectation is violated.

Fallback: if `jsonschema` is not installed, steps degrade to strict json.load
parse-validation only, and the limitation is reported loudly in the summary.

Exit code 0 iff every expectation holds; non-zero otherwise.
"""

import json
import sys
from pathlib import Path

SCHEMA_DIR = Path(__file__).resolve().parent
MANIFEST_SCHEMA_PATH = SCHEMA_DIR / "rx.universe-manifest.v1.schema.json"
PROVENANCE_SCHEMA_PATH = SCHEMA_DIR / "rx.provenance-chain.v1.schema.json"
VALID_EXAMPLE_PATH = SCHEMA_DIR / "examples" / "mythology.universe.json"
INVALID_EXAMPLE_PATH = SCHEMA_DIR / "examples" / "invalid_missing_comma.universe.json"

try:
    import jsonschema

    HAVE_JSONSCHEMA = True
except ImportError:
    HAVE_JSONSCHEMA = False


# A minimal, structurally valid provenance chain used to exercise the
# provenance schema. Hashes/signatures are placeholders of correct shape;
# real chain continuity + signature checks live in the receipt verifier.
INLINE_PROVENANCE_EXAMPLE = {
    "schema_version": "rx.provenance-chain.v1",
    "subject": {"kind": "companion", "id": "mythology:gatewarden-0001"},
    "links": [
        {
            "index": 0,
            "link_type": "ip_owner",
            "issuer": {
                "id": "reflexion",
                "name": "Reflexion",
                "public_key_id": "reflexion-root-key-1",
            },
            "issued_at_tick": 0,
            "content_hash": "a" * 64,
            "previous_link_hash": "0" * 64,
            "signature": {"algorithm": "ed25519", "value": "QUJDREVGRw=="},
        },
        {
            "index": 1,
            "link_type": "character_package",
            "issuer": {
                "id": "reflexion",
                "name": "Reflexion",
                "public_key_id": "reflexion-root-key-1",
            },
            "issued_at_tick": 1,
            "content_hash": "b" * 64,
            "previous_link_hash": "c" * 64,
            "signature": {"algorithm": "ed25519", "value": "QUJDREVGRw=="},
        },
    ],
}


class Report:
    def __init__(self):
        self.entries = []  # (ok: bool, label: str, detail: str)

    def record(self, ok, label, detail=""):
        self.entries.append((ok, label, detail))

    def summary(self):
        lines = []
        for ok, label, detail in self.entries:
            mark = "PASS" if ok else "FAIL"
            line = f"[{mark}] {label}"
            if detail:
                line += f" — {detail}"
            lines.append(line)
        return "\n".join(lines)

    @property
    def all_ok(self):
        return all(ok for ok, _, _ in self.entries)


def strict_load(path, report, label):
    """Strict json.load; records result; returns parsed object or None."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            obj = json.load(f)
        report.record(True, label)
        return obj
    except json.JSONDecodeError as e:
        report.record(
            False, label, f"JSON parse error at line {e.lineno} column {e.colno}: {e.msg}"
        )
        return None
    except OSError as e:
        report.record(False, label, f"cannot read file: {e}")
        return None


def main():
    report = Report()

    if not HAVE_JSONSCHEMA:
        print(
            "WARNING: `jsonschema` package not installed. Falling back to strict\n"
            "json.load parse-validation ONLY. Schema-validity and instance-validation\n"
            "checks are DEGRADED (documents are checked to be well-formed JSON, not\n"
            "to be valid JSON Schema, and examples are not validated against schemas).\n"
        )

    # --- 1. Schemas are well-formed JSON (and valid draft 2020-12 schemas) ---
    manifest_schema = strict_load(
        MANIFEST_SCHEMA_PATH, report, "manifest schema parses as JSON"
    )
    provenance_schema = strict_load(
        PROVENANCE_SCHEMA_PATH, report, "provenance schema parses as JSON"
    )

    if HAVE_JSONSCHEMA:
        for name, schema in (
            ("manifest schema is valid draft 2020-12", manifest_schema),
            ("provenance schema is valid draft 2020-12", provenance_schema),
        ):
            if schema is None:
                report.record(False, name, "skipped: schema did not parse")
                continue
            try:
                jsonschema.Draft202012Validator.check_schema(schema)
                report.record(True, name)
            except jsonschema.SchemaError as e:
                report.record(False, name, f"SchemaError: {e.message}")

    # --- 2. Valid example passes ---
    valid_example = strict_load(
        VALID_EXAMPLE_PATH, report, "valid example (mythology) parses as JSON"
    )
    if HAVE_JSONSCHEMA:
        if manifest_schema is not None and valid_example is not None:
            validator = jsonschema.Draft202012Validator(manifest_schema)
            errors = sorted(
                validator.iter_errors(valid_example), key=lambda e: list(e.path)
            )
            if errors:
                first = errors[0]
                loc = "/".join(str(p) for p in first.path) or "<root>"
                report.record(
                    False,
                    "valid example passes manifest schema",
                    f"{len(errors)} error(s); first at {loc}: {first.message}",
                )
            else:
                report.record(True, "valid example passes manifest schema")
        else:
            report.record(
                False, "valid example passes manifest schema", "skipped: parse failure"
            )

        # --- 3. Inline provenance example passes ---
        if provenance_schema is not None:
            validator = jsonschema.Draft202012Validator(provenance_schema)
            errors = sorted(
                validator.iter_errors(INLINE_PROVENANCE_EXAMPLE),
                key=lambda e: list(e.path),
            )
            if errors:
                first = errors[0]
                loc = "/".join(str(p) for p in first.path) or "<root>"
                report.record(
                    False,
                    "inline provenance example passes provenance schema",
                    f"{len(errors)} error(s); first at {loc}: {first.message}",
                )
            else:
                report.record(True, "inline provenance example passes provenance schema")
        else:
            report.record(
                False,
                "inline provenance example passes provenance schema",
                "skipped: schema parse failure",
            )

    # --- 4. Malformed example fails with a precise error ---
    try:
        with open(INVALID_EXAMPLE_PATH, "r", encoding="utf-8") as f:
            json.load(f)
        report.record(
            False,
            "malformed example is rejected",
            "EXPECTATION VIOLATED: file parsed cleanly but must not",
        )
    except json.JSONDecodeError as e:
        report.record(
            True,
            "malformed example is rejected",
            f"rejected at line {e.lineno} column {e.colno}: {e.msg}",
        )
    except OSError as e:
        report.record(False, "malformed example is rejected", f"cannot read file: {e}")

    # --- Summary ---
    print(report.summary())
    print()
    if report.all_ok:
        suffix = "" if HAVE_JSONSCHEMA else " (parse-only fallback — see WARNING above)"
        print(f"OVERALL: PASS{suffix}")
        return 0
    print("OVERALL: FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
