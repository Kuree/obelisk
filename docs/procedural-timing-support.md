# Procedural timing support

This matrix records the executable boundary implemented by the current
vertical slices. “Executable” means the checked source subset lowers through
both native and bytecode tiers with differential trace coverage. It does not
mean that the umbrella IEEE 1800 procedural-synchronization work is complete.

| Form | Current boundary |
| --- | --- |
| Integral `#delay` | Executable for constant and dynamic packed integral expressions up to 64 bits; X/Z and negative values normalize to zero. Static overflow is diagnosed and dynamic scaling is range-checked identically in native and bytecode. |
| Literal real/realtime delay | Executable for real and time literals, including unary sign; values round at lexical `timeprecision` before design-precision scaling. Dynamic real arithmetic is not yet executable. |
| `#0` | Executable in a distinct Inactive queue for design-domain processes. Native and design-bytecode work share the same region/rank/insertion ordering key. Program Re-Inactive placement remains pending. |
| `#1step` | Executable as one design-precision tick. |
| Direct signal event | Executable for statically addressable signal/net expressions and change/posedge/negedge/both-edge. Vector edges observe only the edge-defining bit. A directly addressable `iff` value is sampled and latched at the primary occurrence. |
| Event list | Executable for direct and computed signal/net members, including mixed lists and computed or mixed `iff` conditions. All-direct lists retain the handle-based fast path; a list containing a computed member uses source-ordered observers. A single named event retains the direct-event path. |
| Computed event expression | Executable for change/posedge/negedge/both-edge controls, four-state values, vector LSB edge rules, dynamic selections, and transient same-fragment occurrences. Dependencies watch complete root objects plus dynamic index inputs so retargeting cannot leave stale subscriptions. |
| `@*` | Executable for dependencies discovered from lowered controlled-statement reads and transitive zero-time callee read summaries; write-only captures are excluded. |
| Repeated event control | Executable, including zero and dynamic counts. |
| `wait (expr)` | Executable for constants, directly addressable packed values, and computed expressions. A true occurrence is latched at the relevant publication, including same-fragment transients. Computed evaluators support the executable zero-time expression and call subset. |
| `##` cycle control | Not executable; default-clocking resolution and counted clock subscriptions remain pending. |
| Static named event | Executable for fresh uninitialized design events, direct waits/triggers, equality, and `.triggered`, including computed reads such as `wait (event.triggered)`. |
| Event initialization/assignment/null | Rejected pending event cells, nullable handles, and automatic event lifetime. |
| Blocking `->` | Executable for supported static named events. |
| Nonblocking `->>` | Executable with optional integral delay through scheduler-owned event commits. Program Re-NBA placement remains pending. |
| `wait_order` | Imported with unambiguous inventory, then rejected with a targeted occurrence-order diagnostic. |
| Blocking intra-assignment timing | Executable for delay, direct event, and repeated-event controls. RHS is captured at encounter; LHS is resolved at commit. |
| Nonblocking intra-assignment timing | Executable for delay controls with encounter-time LHS/RHS capture. Event/repeat controls remain pending deferred actions. |
| `fork` / join forms | Imported with block-kind metadata, then rejected pending branch outlining and child-process synchronization. |
| `wait fork` / `disable fork` | Targeted diagnostics; descendant registry and cancellation are pending. |
| Timed/recursive task calls | Pending canonical task frames and suspension-capable calls. Existing executable functions remain zero-time. |
| Module/program domain | IR carries and verifies Design/Active versus Program/Reactive home metadata. Design-domain native and bytecode processes use shared Active/Inactive ordering, including continuation ranks. The remaining IEEE regions and program-domain Re-Inactive/Re-NBA behavior are pending. |

The umbrella is complete only when the pending rows are implemented and the
native, bytecode, mixed-tier, region-ordering, cancellation, and leak
conformance matrix is green.

Computed observers run under the waiting logical process and sample `iff` only
after a matching primary occurrence. Signal, resolved-net, NBA, named-event,
and external publications all participate in the same recursive execution
transaction; nested observer writes are evaluated synchronously and bounded by
the runtime resource limit. Native and whole-design bytecode use the same
serialized capture/result contract.
