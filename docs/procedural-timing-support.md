# Procedural timing support

This matrix records the executable boundary implemented by the current
vertical slices. “Executable” means the checked source subset lowers through
both native and bytecode tiers with differential trace coverage. It does not
mean that the umbrella IEEE 1800 procedural-synchronization work is complete.

| Form | Current boundary |
| --- | --- |
| Integral `#delay` | Executable for constant and dynamic packed integral expressions up to 64 bits; X/Z and negative values normalize to zero. Static overflow is diagnosed and dynamic scaling is range-checked identically in native and bytecode. |
| Real/realtime delay | Executable for literals and dynamic binary32/binary64 expressions, including unary sign; values round at lexical `timeprecision` before design-precision scaling. |
| `#0` | Executable in Inactive for design-domain processes and Re-Inactive for program-domain processes. Native and design-bytecode work share the same region/rank/insertion ordering key. |
| `#1step` | Executable as one design-precision tick. |
| Direct signal event | Executable for statically addressable signal/net expressions and change/posedge/negedge/both-edge. Real-valued change events use IEEE equality (`+0.0` and `-0.0` are equal; every NaN publication changes). Vector edges observe only the edge-defining bit. A directly addressable `iff` value is sampled and latched at the primary occurrence. |
| Event list | Executable for direct and computed signal/net members, including mixed lists and computed or mixed `iff` conditions. All-direct lists retain the handle-based fast path; a list containing a computed member uses source-ordered observers. A single named event retains the direct-event path. |
| Computed event expression | Executable for change/posedge/negedge/both-edge controls, four-state values, vector LSB edge rules, dynamic selections, and transient same-fragment occurrences. Dependencies watch complete root objects plus dynamic index inputs so retargeting cannot leave stale subscriptions. |
| `@*` | Executable for dependencies discovered from lowered controlled-statement reads and transitive zero-time callee read summaries; write-only captures are excluded. |
| Repeated event control | Executable, including zero and dynamic counts. |
| `wait (expr)` | Executable for constants, directly addressable packed values, and computed expressions. A true occurrence is latched at the relevant publication, including same-fragment transients. Computed evaluators support the executable zero-time expression and call subset. |
| `##` cycle control | Not executable; default-clocking resolution and counted clock subscriptions remain pending. |
| Static named event | Executable for fresh uninitialized design events, direct waits/triggers, equality, and `.triggered`, including computed reads such as `wait (event.triggered)`. |
| Event initialization/assignment/null | Rejected pending event cells, nullable handles, and automatic event lifetime. |
| Blocking `->` | Executable for supported static named events. |
| Nonblocking `->>` | Executable with optional integral delay through scheduler-owned event commits. Commits use NBA for design-domain callers and Re-NBA for program-domain callers. |
| `$strobe` family | Executable as one-shot Postponed processes. Arguments are re-evaluated after same-slot NBA/Re-NBA commits, and strobes complete before final blocks. |
| `$monitor` family | Executable as one context-wide persistent Postponed process, including replacement by a later `$monitor`, once-per-changed-slot output, and `$monitoron`/`$monitoroff`. |
| `wait_order` | Imported with unambiguous inventory, then rejected with a targeted occurrence-order diagnostic. |
| Blocking intra-assignment timing | Executable for delay, direct event, and repeated-event controls. RHS is captured at encounter; LHS is resolved at commit. |
| Nonblocking intra-assignment timing | Executable for delay controls with encounter-time LHS/RHS capture. Event/repeat controls remain pending deferred actions. |
| `fork` / join forms | Imported with block-kind metadata, then rejected pending branch outlining and child-process synchronization. |
| `wait fork` / `disable fork` | Targeted diagnostics; descendant registry and cancellation are pending. |
| Timed/recursive task calls | Pending canonical task frames and suspension-capable calls. Existing executable functions remain zero-time. |
| Module/program domain | Executable with Design/Active and Program/Reactive homes. `#0` maps to Inactive/Re-Inactive and nonblocking commits map to NBA/Re-NBA. Region, rank, and insertion sequence form one shared native/bytecode ordering key. Observed remains reserved for the assertion milestone. |

The umbrella is complete only when the pending rows are implemented and the
native, bytecode, mixed-tier, region-ordering, cancellation, and leak
conformance matrix is green.

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
