"""Shared data types for the benchmark harness.

Deliberately minimal: a pin for a fetchable suite, and a per-test outcome. Suite
drivers produce `Outcome`s; the CLI aggregates and classifies them. Keeping the
status vocabulary here (rather than as ad-hoc strings) is what lets one reporting
path serve every suite.
"""

from __future__ import annotations

import dataclasses

# Per-test status vocabulary shared by all suites.
#   pass        - simulated to completion and self-checked as correct
#   xfail_pass  - upstream counts it "passed" because Obelisk errored in the
#                 phase where failure was expected; not a completed positive run
#   compile_fail- Obelisk failed to compile the design
#   run_fail    - compiled, but did not finish / did not self-check
#   skip        - upstream skipped it (missing feature, unsupported scenario)
PASS = "pass"
XFAIL_PASS = "xfail_pass"
COMPILE_FAIL = "compile_fail"
RUN_FAIL = "run_fail"
SKIP = "skip"


@dataclasses.dataclass(frozen=True)
class GitSource:
    """A suite's upstream repository pinned to an exact revision."""
    url: str
    rev: str


@dataclasses.dataclass
class Outcome:
    """The result of one test: a status plus the compile log for classification.

    `log` is the raw compiler diagnostics text (empty when the test never
    reached compilation); classify.py buckets it into features.
    """
    status: str
    log: str = ""


def summarize(outcomes: dict[str, Outcome]) -> dict:
    """Reduce per-test outcomes to the count fields stored in a history record."""
    counts = {PASS: 0, XFAIL_PASS: 0, COMPILE_FAIL: 0, RUN_FAIL: 0, SKIP: 0}
    for outcome in outcomes.values():
        counts[outcome.status] = counts.get(outcome.status, 0) + 1
    return {
        "total": len(outcomes),
        "passed": counts[PASS],
        "xfail_passed": counts[XFAIL_PASS],
        "compile_fail": counts[COMPILE_FAIL],
        "run_fail": counts[RUN_FAIL],
        "skipped": counts[SKIP],
    }
