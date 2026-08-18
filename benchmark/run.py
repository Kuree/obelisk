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
import sys
import textwrap
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from obelisk_bench import classify, history, locate, model, runner  # noqa: E402
from obelisk_bench.suites import REGISTRY  # noqa: E402


def _print_summary(summary: dict) -> None:
    print(f"\n{summary['total']} tests: "
          f"{summary['passed']} passed, "
          f"{summary['xfail_passed']} expected-error passed, "
          f"{summary['compile_fail']} compile-fail, "
          f"{summary['run_fail']} run-fail, "
          f"{summary['skipped']} skipped")


def _print_skips(outcomes: dict[str, model.Outcome]) -> None:
    """List the tests a suite excluded, grouped by the reason it gave.

    A skip is neither a pass nor a failure, so the count alone would hide what
    the suite decided not to measure. Printing the reasons keeps that decision
    in front of whoever reads the run.
    """
    reasons: dict[str, list[str]] = {}
    for name, outcome in sorted(outcomes.items()):
        if outcome.status == model.SKIP:
            reasons.setdefault(outcome.log, []).append(name)
    if not reasons:
        return
    print("\nSkipped:")
    for reason, names in sorted(reasons.items(), key=lambda item: item[1]):
        print(textwrap.fill(", ".join(names), width=78,
                            initial_indent="  ", subsequent_indent="  "))
        print(textwrap.fill(reason or "no reason given", width=78,
                            initial_indent="    ", subsequent_indent="    "))
    print("  A skip is neither a pass nor a failure: the suite decided the "
          "expectation is not\n  Obelisk's to meet, and named the clause it "
          "read to decide that.")


def _print_failures(outcomes: dict[str, model.Outcome]) -> None:
    failures = [
        (name, outcome)
        for name, outcome in sorted(outcomes.items())
        if outcome.status in (model.COMPILE_FAIL, model.RUN_FAIL)
    ]
    if not failures:
        return
    print("\nFailure details:")
    for name, outcome in failures:
        print(f"\n[{outcome.status}] {name}")
        print(outcome.log.rstrip() or "(no diagnostic output)")


def command_run(args) -> int:
    suite = REGISTRY[args.suite]
    args.obelisk_binary = str(locate.resolve_compiler(args.obelisk))
    root = locate.resolve_suite_root(suite.NAME, args.suite_root, args.fetch,
                                     suite.SOURCE)

    outcomes = suite.run(root, args)

    summary = model.summarize(outcomes)
    _print_summary(summary)
    _print_skips(outcomes)
    if args.show_failures:
        _print_failures(outcomes)

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
        sub.add_argument(
            "--vpi", choices=("off", "read", "full"), default=None,
            help="Obelisk VPI capability mode (default: full with VPI code, "
                 "off otherwise)")
        sub.add_argument(
            "--vpi-code", action="append", default=[], metavar="PATH",
            help="C/C++/native VPI module input to attach to every test "
                 "(repeatable)")
        sub.add_argument("-j", "--jobs", type=int,
                         default=runner.available_cpu_count(),
                         help="parallel workers (default: all cores)")
        sub.add_argument("--timeout", type=float, default=10.0,
                         help="per-test execution timeout in seconds (default: 10)")
        sub.add_argument("--record", action="store_true",
                         help="append this run to the suite's history")
        sub.add_argument("--greedy", action="store_true",
                         help="also print the greedy unblock ordering")
        sub.add_argument("--show-failures", action="store_true",
                         help="print each compile/run failure diagnostic")
        sub.add_argument("--lists", nargs="*", default=None,
                         help="ivtest list files to run (ivtest only)")
        if name == "svtests":
            sub.add_argument("--design", default=None,
                             help="compile a third_party/cores/ design from its "
                                  "explicit file list (use 'all' for every core)")
            sub.add_argument(
                "--no-resume", action="store_true",
                help="ignore cached successful tests and run the selection fresh")
            sub.add_argument(
                "--result-cache", default=None, metavar="PATH",
                help="successful-result checkpoint (default: "
                     "benchmark/cache/results/svtests.json)")
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
