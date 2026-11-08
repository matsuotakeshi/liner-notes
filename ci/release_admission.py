#!/usr/bin/env python3
"""Fail-closed GitHub Actions admission evidence verifier (no SSH/deployment).

The authenticated GitHub API responses are passed as local JSON documents.
This only checks workflow provenance and an existing release-set receipt; it
does not sign artifacts, verify backups, or authorize a production deployment.
"""
import argparse
import json
import re
import sys
from pathlib import Path

HEX_SHA = re.compile(r"[0-9a-f]{40}\Z")
RECEIPT_NAME = "liner-notes-release-set-receipt"
GATES = (
    ("native", "push"),
    ("freebsd", "push"),
    ("rocky", "push"),
    ("debian", "push"),
    ("release_set", "workflow_run"),
    ("connectivity", "workflow_dispatch"),
)


def require_sha(source_sha):
    if not isinstance(source_sha, str) or not HEX_SHA.fullmatch(source_sha):
        raise ValueError("A full lowercase 40-character main commit SHA is required")
    return source_sha


def positive_integer(value):
    return type(value) is int and value > 0


def read_json(path):
    with Path(path).open(encoding="utf-8") as stream:
        return json.load(stream)


def latest_successful_run(document, source_sha, event, label):
    """Reject missing, stale, failed or wrong-event workflow evidence."""
    require_sha(source_sha)
    if not isinstance(document, dict) or not isinstance(document.get("workflow_runs"), list):
        raise ValueError(f"{label}: invalid workflow runs API response")
    candidates = []
    for run in document["workflow_runs"]:
        if not isinstance(run, dict):
            raise ValueError(f"{label}: malformed run")
        if (run.get("head_sha"), run.get("head_branch"), run.get("event")) != (
            source_sha, "main", event
        ):
            continue
        if not all(positive_integer(run.get(key)) for key in ("id", "run_number", "run_attempt")):
            raise ValueError(f"{label}: invalid run identifiers")
        candidates.append(run)
    if not candidates:
        raise ValueError(f"{label}: no exact-main {event} run for this commit")
    # A prior green run cannot cover up a newer failed or incomplete attempt.
    latest = max(candidates, key=lambda r: (r["run_number"], r["run_attempt"], r["id"]))
    if latest.get("status") != "completed" or latest.get("conclusion") != "success":
        raise ValueError(f"{label}: most recent exact-main run is not successful")
    return latest["id"]


def verify_gates(source_sha, documents):
    if set(documents) != {name for name, _ in GATES}:
        raise ValueError("Missing or extra admission gate")
    successful = {}
    for name, event in GATES:
        successful[name] = latest_successful_run(
            documents[name], source_sha, event, name
        )
    return successful


def verify_receipt(document, run_id, source_sha):
    require_sha(source_sha)
    if not positive_integer(run_id):
        raise ValueError("Invalid release verification run ID")
    if not isinstance(document, dict) or not isinstance(document.get("artifacts"), list):
        raise ValueError("Invalid artifact list API response")
    matching = [
        artifact for artifact in document["artifacts"]
        if isinstance(artifact, dict) and artifact.get("name") == RECEIPT_NAME
    ]
    if len(matching) != 1:
        raise ValueError("Missing or duplicate release-set receipt artifact")
    artifact = matching[0]
    origin = artifact.get("workflow_run")
    if (artifact.get("expired") is not False
            or not positive_integer(artifact.get("id"))
            or not positive_integer(artifact.get("size_in_bytes"))
            or not isinstance(origin, dict)
            or origin.get("id") != run_id
            or origin.get("head_sha") != source_sha
            or origin.get("head_branch") != "main"):
        raise ValueError("Release-set receipt does not match the successful main run")
    return artifact["id"]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    gates = sub.add_parser("gates")
    gates.add_argument("--source-sha", required=True)
    for label, _ in GATES:
        gates.add_argument(f"--{label.replace('_', '-')}-json", required=True)
    receipt = sub.add_parser("receipt")
    receipt.add_argument("--source-sha", required=True)
    receipt.add_argument("--run-id", type=int, required=True)
    receipt.add_argument("--artifacts-json", required=True)
    args = p.parse_args(argv)
    try:
        if args.command == "gates":
            documents = {
                name: read_json(getattr(args, f"{name}_json")) for name, _ in GATES
            }
            runs = verify_gates(args.source_sha, documents)
            # Only the run ID is emitted for the workflow's next API request.
            print(runs["release_set"])
        else:
            artifact_id = verify_receipt(
                read_json(args.artifacts_json), args.run_id, args.source_sha
            )
            print(f"PASS: verified source-SHA-matched release receipt artifact {artifact_id}")
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as error:
        print(f"RELEASE ADMISSION REJECTED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
