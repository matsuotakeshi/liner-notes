#!/usr/bin/env python3
"""PR-safe synthetic coverage for the production admission gate; no SSH."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))
from release_admission import GATES, latest_successful_run, verify_gates, verify_receipt

SHA = "a" * 40
OTHER = "b" * 40


def run(number=1, event="push", sha=SHA, branch="main", conclusion="success",
        status="completed", attempt=1):
    return dict(
        id=number + 1000, run_number=number, run_attempt=attempt,
        event=event, head_sha=sha, head_branch=branch, conclusion=conclusion,
        status=status,
    )


def fixture():
    return {
        name: {"workflow_runs": [run(i + 1, event)]}
        for i, (name, event) in enumerate(GATES)
    }


def receipt(run_id=1005, sha=SHA, expired=False, count=1, size=941):
    return {
        "artifacts": [
            dict(
                name="liner-notes-release-set-receipt",
                id=2000 + i, expired=expired, size_in_bytes=size,
                workflow_run={"id": run_id, "head_sha": sha, "head_branch": "main"},
            )
            for i in range(count)
        ]
    }


class AdmissionTests(unittest.TestCase):
    def test_successful_exact_main_package_and_receipt_evidence(self):
        runs = verify_gates(SHA, fixture())
        self.assertEqual(runs["release_set"], 1005)
        self.assertEqual(verify_receipt(receipt(), runs["release_set"], SHA), 2000)

    def test_missing_or_wrong_branch_event_and_commit_are_rejected(self):
        for name, _ in GATES:
            with self.subTest(missing=name):
                documents = fixture()
                documents[name]["workflow_runs"] = []
                with self.assertRaises(ValueError):
                    verify_gates(SHA, documents)
            for field, value in (
                ("head_sha", OTHER),
                ("head_branch", "ci/test"),
                ("event", "workflow_dispatch" if name != "connectivity" else "push"),
            ):
                with self.subTest(name=name, field=field):
                    documents = fixture()
                    documents[name]["workflow_runs"][0][field] = value
                    with self.assertRaises(ValueError):
                        verify_gates(SHA, documents)

    def test_newer_failed_or_pending_run_must_not_reuse_old_success(self):
        documents = fixture()
        for state, conclusion in (("completed", "failure"), ("in_progress", None)):
            with self.subTest(state=state):
                documents["release_set"]["workflow_runs"].append(
                    run(12, "workflow_run", conclusion=conclusion, status=state)
                )
                with self.assertRaises(ValueError):
                    verify_gates(SHA, documents)
                documents["release_set"]["workflow_runs"].pop()

    def test_invalid_ids_or_sha_fail_closed(self):
        documents = fixture()
        for invalid_sha in ("abc", OTHER.upper(), OTHER[:-1], ""):
            with self.subTest(invalid_sha=invalid_sha):
                with self.assertRaises(ValueError):
                    verify_gates(invalid_sha, documents)
        for value in (0, -1, True, "1005", None):
            with self.subTest(value=value):
                documents = fixture()
                documents["native"]["workflow_runs"][0]["id"] = value
                with self.assertRaises(ValueError):
                    verify_gates(SHA, documents)

    def test_receipt_absent_expired_wrong_run_or_source(self):
        for document, run_id in (
            ({"artifacts": []}, 1005),
            (receipt(count=2), 1005),
            (receipt(expired=True), 1005),
            (receipt(size=0), 1005),
            (receipt(run_id=999), 1005),
            (receipt(sha=OTHER), 1005),
            (receipt(), 0),
            ({"artifacts": "not a list"}, 1005),
        ):
            with self.subTest(document=document, run_id=run_id):
                with self.assertRaises(ValueError):
                    verify_receipt(document, run_id, SHA)

    def test_malformed_gates_and_manual_release_audits_are_rejected(self):
        documents = fixture()
        documents["release_set"]["workflow_runs"][0]["event"] = "workflow_dispatch"
        with self.assertRaises(ValueError):
            verify_gates(SHA, documents)
        with self.assertRaises(ValueError):
            latest_successful_run({"workflow_runs": {}}, SHA, "push", "native")
        with self.assertRaises(ValueError):
            verify_gates(SHA, {"native": fixture()["native"]})


if __name__ == "__main__":
    unittest.main(verbosity=2)
