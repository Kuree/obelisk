# Procedural timing support

This matrix records the executable procedural-timing boundary. “Executable”
means the checked source subset lowers through both native and bytecode tiers
with differential trace coverage. It does not
mean that the umbrella IEEE 1800 procedural-synchronization work is complete.
The matrix assumes semantically valid source: frontend diagnostics reject the
LRM-prohibited automatic-variable targets and event-control references of a
nonblocking assignment before this lowering boundary.

| Form | Current boundary |
| --- | --- |
| Integral `#delay` | Executable for constant and dynamic packed integral expressions up to 64 bits; X/Z and negative values normalize to zero. Static overflow is diagnosed and dynamic scaling is range-checked identically in native and bytecode. |
| Real/realtime delay | Executable for literals and dynamic binary32/binary64 expressions, including unary sign; values round at lexical `timeprecision` before design-precision scaling. |
| Min/typ/max expression (11.11) | Executable for constant and dynamic selected expressions. The public `--timing=min\|typ\|max` option selects one branch during frontend lowering (default `typ`), and only that branch reaches executable IR; this applies uniformly to delay expressions and ordinary expression contexts. |
| `#0` | Executable in Inactive for design-domain processes and Re-Inactive for program-domain processes. Native and design-bytecode work share the same region/rank/insertion ordering key. |
| `#1step` | Executable as one design-precision tick. |
| Direct signal event | Executable for statically addressable signal/net expressions and change/posedge/negedge/both-edge. Real-valued change events use IEEE equality (`+0.0` and `-0.0` are equal; every NaN publication changes). Vector edges observe only the edge-defining bit. A directly addressable `iff` value is sampled and latched at the primary occurrence. |
| Event list | Executable for direct and computed signal/net members, including mixed lists and computed or mixed `iff` conditions. All-direct lists retain the handle-based fast path; a list containing a computed member uses source-ordered observers. A single named event retains the direct-event path. |
| Computed event expression | Executable for change/posedge/negedge/both-edge controls, four-state values, vector LSB edge rules, dynamic selections, and transient same-fragment occurrences. Dependencies watch complete root objects plus dynamic index inputs so retargeting cannot leave stale subscriptions. |
| `@*` | Executable for dependencies discovered from lowered controlled-statement reads and transitive zero-time callee read summaries; write-only captures are excluded. |
| Repeated event control | Executable, including zero and dynamic counts. |
| `wait (expr)` | Executable for constants, directly addressable packed values, and computed expressions. A true occurrence is latched at the relevant publication, including same-fragment transients. Computed evaluators support the executable zero-time expression and call subset. |
| Concurrent assertion cycle delays | The bounded and selected unbounded forms are documented in the [assertion conformance matrix](sva-lrm-support.md). |
| Static named event | Executable for fresh uninitialized design events, direct waits/triggers, equality, and `.triggered`, including computed reads such as `wait (event.triggered)`. |
| Event initialization/assignment/null | Executable for design/static and automatic scalar event variables. An uninitialized declaration creates a fresh synchronization object; declaration initialization and procedural assignment copy the event handle, and `null` breaks the association. A waiter retains the handle captured when it suspended, so later reassignment does not migrate an existing wait queue. Fixed aggregate event elements retain independent objects. |
| Blocking `->` | Executable for supported static named events. |
| Nonblocking `->>` | Executable with optional integral delay through scheduler-owned event commits. Commits use NBA for design-domain callers and Re-NBA for program-domain callers. |
| `$strobe` family | Executable as one-shot Postponed processes. Arguments are re-evaluated after same-slot NBA/Re-NBA commits, and strobes complete before final blocks. |
| `$monitor` family | Executable as one context-wide persistent Postponed process, including replacement by a later `$monitor`, once-per-changed-slot output, and `$monitoron`/`$monitoroff`. |
| `wait_order` | Executable for ordered lists of supported named-event handles. The scheduler consumes occurrences left to right, ignores retriggers of already-consumed events, and fails when an unconsumed later event triggers first. Only the first event may consume the current time slot's persistent `.triggered` state when the wait begins. Success and explicit failure actions lower to ordinary control flow; without a failure action, failure records the IEEE run-time error and execution continues. An aliased later handle fails when its shared synchronization object triggers, and cancellation removes the ordered wait in both native and bytecode tiers. |
| Blocking intra-assignment timing | Executable for delay, direct event, and repeated-event controls. RHS is captured at encounter; LHS is resolved at commit. |
| Nonblocking intra-assignment timing | Executable for delay, direct event (including `iff`), event-list, computed-event, and repeated-event controls with encounter-time LHS/RHS capture. Event and positive repeated-event controls launch detached scheduler-owned waiters; a nonpositive repeat count stages the NBA immediately in source order. |
| Immediate assertions | Ordinary and deferred forms are documented in the [assertion conformance matrix](sva-lrm-support.md). |
| Concurrent assertions | The executable monitor subset, region behavior, and diagnostics are documented in the [assertion conformance matrix](sva-lrm-support.md). |
| Sampled-value functions | The supported sampled-value and global-clock families are documented in the [assertion conformance matrix](sva-lrm-support.md). |
| `fork` / join forms | Executable for `join`, `join_any`, and `join_none`, including empty forks, automatic declaration capture, nested descendants, and `join_none` spawned from a zero-time function. Other fork forms in a zero-time function are rejected because they can block. |
| `wait fork` / `disable fork` | Executable through the logical-process descendant registry. `wait fork` observes outstanding descendants and `disable fork` recursively cancels the current process's live descendants without affecting unrelated processes. |
| Named block disable | Executable for lexical, named-fork, and resolved hierarchical targets. Cancellation propagates through nested task calls and suppresses pending task copy-out. |
| Timed/recursive task calls | Executable for direct static and automatic tasks with value, output, inout, and ref formals, early return, suspension, direct and mutual recursion, and cancellation. Class tasks use the same suspension-capable call path. Functions remain zero-time, except that they may launch `join_none` children. |
| Module/program domain | Executable with Design/Active and Program/Reactive homes. Assertion evaluators additionally use Design/Observed, assertion action callbacks use Design/Reactive, and final deferred assertions use Design/Postponed. `#0` maps to Inactive/Re-Inactive and nonblocking commits map to NBA/Re-NBA. Region, rank, and insertion sequence form one shared native/bytecode ordering key. |

Full procedural-timing coverage still requires the unsupported cases named
above and in the assertion matrix, plus native, bytecode, mixed-tier,
region-ordering, cancellation, and lifetime regressions.

The runtime materializes eight executable ordinals: Active, Inactive, NBA,
Observed, Reactive, Re-Inactive, Re-NBA, and Postponed. Preponed is a
time-slot-boundary hook; the eight PLI callback regions remain compiler enum
points and fold exactly while the runtime has no VPI callback producer.

Computed observers run under the waiting logical process and sample `iff` only
after a matching primary occurrence. Signal, resolved-net, NBA, named-event,
and external publications all participate in the same recursive execution
transaction; nested observer writes are evaluated synchronously and bounded by
the runtime resource limit. Native and whole-design bytecode use the same
serialized capture/result contract.
