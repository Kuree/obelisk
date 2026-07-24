#!/usr/bin/env python3

"""Run Obelisk against an external SystemVerilog test suite and report progress.

This is the entry point for the conformance benchmark. It resolves the Obelisk
binary and a suite checkout, drives the suite through the shared shims, classifies
the diagnostics into a feature-level roadmap, and — with --record — appends the
run to the suite's tracked history so the pass rate can be watched over time.

Examples:
    python3 benchmark/run.py verilator --suite-root /path/to/verilator
    python3 benchmark/run.py ivtest --lists regress-sv.list --record
    python3 benchmark/run.py report --history verilator
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from obelisk_bench import classify, history, locate, model  # noqa: E402
from obelisk_bench.suites import REGISTRY  # noqa: E402


def _print_summary(summary: dict) -> None:
    print(f"\n{summary['total']} tests: "
          f"{summary['passed']} passed, "
          f"{summary['xfail_passed']} expected-error passed, "
          f"{summary['compile_fail']} compile-fail, "
          f"{summary['run_fail']} run-fail, "
          f"{summary['skipped']} skipped")


def command_run(args) -> int:
    suite = REGISTRY[args.suite]
    args.obelisk_binary = str(locate.resolve_compiler(args.obelisk))
    root = locate.resolve_suite_root(suite.NAME, args.suite_root, args.fetch,
                                     suite.SOURCE)

    outcomes = suite.run(root, args)

    summary = model.summarize(outcomes)
    _print_summary(summary)

    # Only compile-failure diagnostics feed the blocker table: those name a
    # missing feature. A run-fail carries program stdout, not a diagnostic, so
    # classifying it would just be noise — it is counted, not ranked.
    logs = {name: outcome.log for name, outcome in outcomes.items()
            if outcome.log and outcome.status == model.COMPILE_FAIL}
    counters = classify.tally(logs)
    print("\n" + classify.format_table(counters))
    if args.greedy:
        print(classify.greedy_order(counters["needs"]))

    if args.record:
        blockers = dict(counters["blocks"].most_common(20))
        record = history.build_record(
            suite.NAME,
            locate.obelisk_revision(),
            locate.suite_revision(root),
            summary,
            blockers,
        )
        path = history.append_record(suite.NAME, record)
        print(f"Recorded run to {path}")
    return 0


def command_report(args) -> int:
    print(history.format_history(args.history), end="")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)

    for name in REGISTRY:
        sub = subparsers.add_parser(name, help=f"run the {name} suite")
        sub.add_argument("--suite-root", help="local checkout to use instead of the cache")
        sub.add_argument("--fetch", action="store_true",
                         help="fetch the pinned revision into benchmark/cache/")
        sub.add_argument("--obelisk", help="path to the Obelisk driver binary")
        sub.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1,
                         help="parallel workers (default: all cores)")
        sub.add_argument("--timeout", type=float, default=10.0,
                         help="per-test execution timeout in seconds (default: 10)")
        sub.add_argument("--record", action="store_true",
                         help="append this run to the suite's history")
        sub.add_argument("--greedy", action="store_true",
                         help="also print the greedy unblock ordering")
        sub.add_argument("--lists", nargs="*", default=None,
                         help="ivtest list files to run (ivtest only)")
        sub.add_argument("tests", nargs="*",
                         help="specific tests to run instead of the full corpus")
        sub.set_defaults(func=command_run, suite=name)

    report = subparsers.add_parser("report", help="render a suite's recorded history")
    report.add_argument("--history", required=True, choices=list(REGISTRY),
                        help="suite whose history to render")
    report.set_defaults(func=command_report)

    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
