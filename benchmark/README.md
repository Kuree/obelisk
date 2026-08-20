# Conformance benchmark

This directory measures Obelisk against external SystemVerilog test suites and
records the result over time. It is not part of `check-obelisk` — that suite is
hermetic and gates the build; this one is a measurement instrument that answers
two questions: *how much of real-world SystemVerilog does Obelisk run today*, and
*what should be built next*.

Missing features get discovered here. `$finish` was found this way.

## How it works

The harness owns its run loop — it does **not** invoke the upstream test drivers
(`driver.py`, `vvp_reg`) or any reference simulator. For each test it compiles the
design with Obelisk directly, runs the resulting native executable, and judges it
three ways:

- a test that expects a **compile error** (`_bad`/`_unsup`, or ivtest type `CE`)
  passes iff Obelisk fails to compile it;
- a test with a **gold file** passes iff its stdout matches the gold;
- otherwise the test **self-checks** and must print its success marker
  (`*-* All Finished *-*` for Verilator, `PASSED` for ivtest).

Real Verilator/Icarus/vvp are never run; the gold files are the ones checked into
each suite. The two suites differ only in what a "test" is:

- **verilator** — the portable `simulator`-scenario corpus (~1700 tests). Each
  design's `module t` is wrapped in a generated clock top-shell (the same one
  Verilator's `driver.py` would produce) so clocked designs advance. Each test
  runs in its own temporary directory, with `t/` linked to the corpus and
  `TEST_OBJ_DIR` pointing at that directory, so a test reads its data file and
  writes its log where upstream's driver puts them. A descriptor asking for
  `timing_loop=True` gets driver.py's other shell, which clocks the design once
  per time unit instead of once per five. A handful are **skipped**:
  see *Skipped tests* below.
- **ivtest** — Icarus's `ivltests` corpus, described by per-test list entries in
  JSON, legacy inline, or VPI-regression form. VPI entries build their C/C++
  sources into a shared module and attach it as a positional native input.

Diagnostics from compile failures are bucketed into named language features
(`classify.py`), so the output is a roadmap, not just a pass count.

## Usage

```sh
# Run a suite against a local checkout:
python3 benchmark/run.py verilator --suite-root /path/to/verilator
python3 benchmark/run.py ivtest --suite-root /path/to/iverilog

# Or fetch the pinned revision into benchmark/cache/ (git-ignored):
python3 benchmark/run.py verilator --fetch

# ivtest list selection (default: the full regress-sv + regress-vlg corpus):
python3 benchmark/run.py ivtest --suite-root /path/to/iverilog --lists obelisk-smoke.list

# Compile and attach VPI code to every test. Source/object/archive/bitcode
# components are linked into a DSO; an existing .so/.vpi is retained directly.
python3 benchmark/run.py verilator --suite-root /path/to/verilator \
  --vpi-code plugin.c --vpi=full

# Run an ivtest VPI-format list. Each entry supplies its own C/C++ module and
# vpi_gold expectation; --vpi=full is selected automatically.
python3 benchmark/run.py ivtest --suite-root /path/to/iverilog \
  --lists obelisk-vpi-smoke.list

# Record the run into the tracked history, and view the curve:
python3 benchmark/run.py verilator --suite-root /path/to/verilator --record
python3 benchmark/run.py report --history verilator

# Also print the greedy "implement these features in this order" ranking:
python3 benchmark/run.py verilator --suite-root /path/to/verilator --greedy
```

Useful flags: `-j` (parallel workers, default all CPUs available to the
process), `--timeout` (per-test execution timeout, default 10s), `--obelisk
PATH` (driver binary),
`--vpi-code PATH` (repeatable native module components), and
`--vpi=off|read|full`. VPI defaults to `full` when code is supplied and `off`
otherwise. `CC` and `CXX` select the native compilers used to form a VPI DSO.
The sv-tests runner divides that same affinity-aware CPU budget across active
tests, preventing each compiler process from creating another full-size thread
pool. UVM-tagged tests run in a separate heavyweight lane capped at half of the
available CPUs and further limited by available memory (budgeted at 4 GiB per
compiler while reserving up to 16 GiB for the desktop and other work).
Successful sv-tests results are checkpointed as they finish and reused on the
next invocation when the compiler/build generation, harness, pinned checkout,
test inputs, and timeout are unchanged. Failures always run again. Runs with
`--vpi-code` conservatively bypass reuse because arbitrary transitive C/C++
headers cannot be identified without a depfile. Use `--no-resume` for a fresh
audit or `--result-cache PATH` to select a different checkpoint file.
The Obelisk binary is otherwise resolved from `OBELISK_BENCH_COMPILER`, then
the repo-relative `build/tools/driver/obelisk`. A suite checkout is resolved
from `--suite-root`, then `OBELISK_BENCH_<SUITE>_ROOT`, then
`benchmark/cache/`, then `--fetch`.

## Reading the output

Pass counts split genuine passes from *expected-error* passes: a test named
`*_bad`/`*_unsup` (Verilator) or typed `CE` (ivtest) "passes" whenever Obelisk
errors for any reason, which is real conformance but not a completed simulation.
Only `passed` counts a run that actually finished; `xfail_passed` is tracked
separately, and those tests' diagnostics are kept out of the blocker table.

The feature table groups each feature by its corresponding IEEE 1800-2017
chapter in the **Area** column and has three counters:

- **blocks** — tests that cannot pass until this feature exists;
- **first** — tests where it is the earliest diagnostic;
- **only** — tests where it is the *sole* blocker, so building it alone should
  carry them to simulation. This is the column to optimize.

Two kinds of row mark classifier gaps rather than described features. `unnamed
construct <x>` / `unnamed type <x>` / `unnamed symbol <x>` are diagnostics that
matched only the generic fallback rules, so the bucket is named after the
construct mnemonic Obelisk printed — grouped and countable, but not yet given a
feature name or an IEEE area. `Unclassified long tail` is what is left: compile
failures whose diagnostics match no rule at all, typically a parse or
elaboration message with no construct in it. Both are the honest measure of what
the classifier still misses; promote a bucket by adding a named rule to `RULES`
in `classify.py` when it grows large enough to matter.

## Skipped tests

A few Verilator tests assert Verilator's own behavior rather than the
language's — `%p` spelling an integer in hex, a four-state variable starting at
zero, an unpacked array assignment pairing elements by storage slot. Passing
them would mean implementing something IEEE 1800-2017 says otherwise about, so
`EXCLUDED` in `suites/verilator.py` names each one with the clause that settles
it, and the run reports them as skips with that citation:

```
Skipped:
  t_dynarray, t_dynarray_method, t_stream_dynamic, t_struct_nest_uarray
    IEEE 1800-2017 21.2.1.7: a singular pattern element prints the way it
    prints unformatted, which 21.2.1 makes decimal; the test expects
    Verilator's hexadecimal with a base prefix
```

A skip is counted apart from both passes and failures, so it can neither
flatter the score nor hide as a bug. Add an entry only after reading the test
and confirming the clause applies — a test that merely looks unfamiliar belongs
in the failure list, where it stays visible. A test the runner cannot set up
(one whose upstream `.py` generates an input file, say) is *not* a skip: it
stays a failure, because that one is the harness's to fix.

## Adding a suite

Write one module under `obelisk_bench/suites/` exposing `NAME`, a pinned
`SOURCE = GitSource(...)`, and `run(root, args) -> dict[str, Outcome]`; add it to
the `REGISTRY` dict in `suites/__init__.py`. There is no base class to subclass
and no framework to configure. Reuse the shared pieces — `runner.py` to compile
and execute, `icarus.py` to translate Icarus flags, `classify.py` for diagnostics,
`model.py` for the status vocabulary — and put only what is genuinely
suite-specific in the module.

## Caveats

- The runner compiles each test with a fixed flag set; it does not execute a
  test's `.py` to recover per-test Verilator flags. On the Icarus path those flags
  are a small tail (most `verilator_flags2` do not apply), so this trades a little
  fidelity for independence from upstream harness internals.
- Self-check-marker judging is the default; explicit gold-diff is exact-match on
  stdout, which a handful of format-sensitive tests may miss.
- Two tests (`t_math_wallace`, `t_math_synmul`) exceed the compile timeout and
  count as compile failures; raise `--timeout`/the compile timeout to include them.

## Layout

```
run.py                     CLI entry point
lists/ivtest/*.list        curated ivtest lists (rescued from the Icarus tree)
history/*.jsonl            tracked progress records, one JSON object per run
obelisk_bench/
  runner.py                compile a design / run a binary (shared)
  icarus.py                Icarus-arg -> Obelisk-flag translation (shared)
  classify.py              diagnostic -> feature rules (shared)
  history.py               append/render run records
  model.py                 shared data types (GitSource, Outcome, statuses)
  locate.py                resolve the binary and suite roots; fetch
  suites/{verilator,ivtest}.py
cache/                     fetched checkouts (git-ignored)
```
