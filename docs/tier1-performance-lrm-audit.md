# Tier-1 performance optimization audit

Reviewed on 2026-09-13 against the local IEEE 1800-2023 text in
`build/lrm-2023.txt`. This is a source-level and regression-backed audit of the
recent performance work, not a proof of complete simulator conformance.
Unrelated web changes and earlier frontend bug fixes are outside this review.

## Finding and fix

The mixed-tier timed-checkpoint drain could update a periodic clock in generated
state and then overwrite it with an older canonical runtime image. This changed
the clock phase on re-entry: an observation at time 4 saw the state from time 1
instead of the result of the rising edge at time 3. This violates the scheduling
and delay semantics in 4.5, 4.9, and 9.4.1, even though the final state could match.

Fixed in `e96d8000`. Before selecting the next action in the cold hybrid drain,
reconcile runtime/VPI dirty roots into the generated planes and publish the
result to canonical state. This also makes preceding native writes visible to
runtime observers. The synchronization is outside the generated steady-state
Tier-1 loop. The existing canonical export is then safe.

Regression: `test/Conversion/simulation-nba-settle-display-runtime.mlir` failed
before the fix and passes after it. It checks reports at times 2, 4, and 6,
pre-NBA immediate display values, a dependent second NBA iteration, and clearing
an initially unknown destination. Checks cover generated coordinator/display
emission and executable output with both requested executor settings; these
settings are not claimed to be independent implementations of the AOT scheduler.

## Reviewed transformations

| Change / commits | LRM constraints and safety argument | Regression coverage |
| --- | --- | --- |
| Empty barriers, dirty-word probes, inactive dynamic slots, root groups (`0ac9eabb`, `70c72b27`, `a3018596`, `37ad6c29`) | 4.4.2.4, 4.5, 4.6, 10.4.2: skip only absent work. The coordinator first drains active ingress, then tests every dirty word **and every independent dynamic valid slot**. Nonempty commits retain region checks, metadata consumption, publication order, and the post-NBA dispatch backedge. No promotion evidence is cleared by the empty exit. | `simulation-direct-output-runtime.mlir`, `simulation-disjoint-dynamic-nba-runtime.mlir` (all four coordinator variants and three commit variants), `simulation-nba-commit-root-groups.mlir`, and the new settling regression. |
| Dynamic NBA lanes and native handoffs (`0208f845`) | 4.6, 4.9.4, 7.4.5, 10.4.2: stage the target index and RHS at enqueue time. Separate lanes require equal strides and disjoint bit ranges within each element, even when indices alias. A site must execute at most once; overlapping/repeated semantic sites retain the runtime route, preserving intermediate transitions. Wide latches are restricted to design-side NBA, not Re-NBA. Invalid indices retain the no-write behavior. | `simulation-disjoint-dynamic-nba-runtime.mlir`; eval disjoint/overlapping/repeated dynamic-NBA tests; repeated-callee and recursive-callee rejection; wide-dynamic-NBA-reactive rejection; persistent-clock-consumer runtime test. |
| Counted procedural clock control (`26fd367c`) | 9.2.1, 9.4.2, 9.6: preserve startup exactly once, the selected edge, carried integer state, and termination. Shared hidden slots require one unconditional root spawn with an unused process handle. Mixed waits, reentrant instances, and unsupported carried values are rejected. A retired phase cannot replay terminal effects. Only pure capture-derived startup values are rematerialized in the evaluator. | `simulation-clocked-control-runtime.mlir`, `simulation-clocked-control-checkpoint-runtime.mlir`, `simulation-clocked-control-reject.mlir`. |
| Terminal-store/path probes (`26fd367c`, `113eb145`) | 4.6 and 6.11.2: speculative probes must not publish state or side effects. A skipped probe store must have no reachable subsequent reference/net read before an excluded checkpoint; calls and other writes reject the probe. Actual execution retains the store. Integer control slots are intrinsically two-state MLIR values, not a license to discard X/Z from language logic values. | `simulation-to-llvm-coroutine-terminal-store-promotion.mlir` includes a store/readback negative case; path-promotion and checkpoint-predicate tests. |
| Direct snapshot display (`05d894ab`) | 4.6, 21.2.1–21.2.3: ordinary display/write executes in place, before its pending NBA, with captured values and the original format environment. Only literal formats, supported scalar snapshots, explicit scope, and stdout qualify. Strength `%v`/`%V`, dynamic formats, managed values, and other channels remain on the canonical path. Monitor/observer entry points and shared helpers are not marked. Probe bodies omit displays. | Direct-output eligibility/runtime tests; `EvalDisplayUsesSnapshotsWithoutMonitorOrSchedulerEffects`; `EvalDisplayRejectsRuntimeOwnedArgumentsAndChannels`; new settling regression; existing strobe/monitor integration test. |
| Packed radix formatting (`2d90b67b`) | 21.2.1.2–21.2.1.3: binary/octal/hex grouping, partial top groups, and X/Z precedence are unchanged. Cross-limb extraction reads the next limb only when valid bits cross it; padding is masked. Width/padding/decimal handling remains in the existing formatter. | `FormatsRadixGroupsAcrossWordAndPartialLimbBoundaries`: widths 1–193, known/X/Z/mixed patterns, absent unknown planes, poisoned padding, and an independently assembled bit-string reference. |
| Cached generated-write ownership (`3418d2bc`) | 10.6.2, 36.9.2: only immutable actor/root membership is cached. Every re-entry still checks current force/assign masks, VPI demand, pending external writes, conditional waiters, and reachable runtime subscriptions. Plan release/reinstallation clears/rebuilds the cache. | `PeriodicWriteFootprintTracksInstalledPlanOwnership`, existing VPI-transition, VPI-backdoor, and persistent-force integration tests. |
| Dormant waveform eligibility (`869096a3`) | 21.7.1: an unexecuted dump operation has no effect. Active waveform collection still rejects periodic execution before and after startup drains. The change is eligibility, not removal of waveform publication. | Dormant-dump-periodic MLIR; existing dump-vcd and dump-vcd-controls integration tests. |
| Checkpoint state, detached deadlines, canonical fallback NBA (`113eb145`, `671d1b8c`, `d747fc6f`) | 4.5, 4.9.4, 9.4.1: resume the exact continuation and preserve already staged NBA effects; restore calendar entries when generated ownership ends; a deoptimized plan must not re-enter its pure generated NBA barrier. The additional mixed-drain coherence defect is fixed above. | Checkpoint-predicate/unconditional-checkpoint MLIR; `RebuiltCalendarRetainsDetachedPeriodicDeadline`; `DeoptimizedNBACommitDoesNotReenterGeneratedBarrier`; new settling regression. |
| Coverage through fusion/periodic execution (`b8ec3d6e`, `113eb145`) | 40.2.2: coverage must reflect executed source work, not dry-run probes. Entry hits stay at startup, wait hits are retained at rearm, clock hits run on actual toggles, and probes do not count. Keepalives follow replaced owners. | Fusion-coverage, coverage-rearm, and checkpoint-coverage MLIR; complete Coverage suite. |
| Block-local plusarg strings (`ef95862b`) | 21.6: only temporary lifetime eligibility changes. Query/matching/parsing operations remain; every allowed string use is in the defining block, with native rooting preserved. Escaping/cross-block values remain unsupported. | `test/Analysis/native-aot-plusargs.mlir`. |
| Prepare classification cache and parallel parameter payloads (`ca15b59a`, `ade9dfd5`) | 24.3.1, 6.20.2: module/program scheduling domains and elaborated parameter values/types must remain unchanged. Cache keys preserve exact instance lookup and the previous fallback-name traversal result. Parallel workers read frozen semantic inventory and build immutable attributes into distinct result slots; IR mutation, symbols, and VPI ordinals remain serial and source ordered. | Program-inventory and invalid-parameter-payload MLIR; VPI-package-parameter serial/threaded output comparison. |

## Validation and limits

- 1,012 Conversion/Analysis/Coverage tests, 613 runtime tests, and 174 process
  runtime tests passed after the fix.
- Nine existing Driver tests passed: Re-NBA, finish/NBA, strobe/monitor,
  VPI transition, VPI backdoor, persistent force, clocked checkpoint, VCD, and
  VCD controls. No new SystemVerilog fixture was introduced.
- Both PicoRV harnesses retain their complete traces and their bounded
  startup/terminal handoffs; ordinary execution remains Tier 1. Diagnostics
  retain one terminal checkpoint and 102/78 startup node executions for the
  stock/legacy harnesses respectively.
- Historical generic/eval LCOV files differ by one execution at five nearby
  lines (1338–1343 of the legacy core), but not in covered/uncovered status.
  These are one `always @*` register-file read block, not lost clocked writes.
  Clause 4.7 permits different active-event interleavings and consequently
  different numbers of combinational reevaluations; 40.2.2 defines statement
  coverage by whether execution occurred. This historical count difference is
  not, by itself, an LRM violation or proof of lost instrumentation. Exact
  cross-mode execution-count equivalence is not claimed by this audit.
- No other concrete semantic violation was found in these transformations.
  This does not establish correctness for every combination of VPI, strengths,
  assertions, reactive scheduling, and asynchronous intervention. Existing
  eligibility guards remain essential and were not weakened by the audit fix.
