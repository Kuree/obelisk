# SystemVerilog assertion conformance

This document tracks Obelisk against the assertion facilities in IEEE Std
1800-2023.  It deliberately distinguishes three levels of support:

- **Executable** means the construct has LRM-oriented simulation semantics in
  both the native and bytecode tiers and has differential tests.
- **Semantic IR** means the elaborated construct and its resolved metadata are
  preserved without claiming that it can be simulated yet.
- **Rejected** means lowering emits a targeted diagnostic.  Rejection is
  preferred to an approximation with different scheduling, sampling, vacuity,
  or endpoint semantics.

The active normative reference is IEEE Std 1800-2023, principally the
assertions and checker chapters and the assertion-related system tasks and
functions.  The standard is available through the IEEE GET program:
<https://standards.ieee.org/ieee/1800/7743/>.

## Conformance matrix

| LRM surface | Current level | Implemented boundary | Work required for full conformance |
| --- | --- | --- | --- |
| Immediate `assert`, `assume`, and `cover` | Executable subset | Four-state truth conversion, explicit action blocks, and default `assert`/`assume` failure diagnostics lower to ordinary control flow. | Complete immediate-cover evaluation/success accounting and all assertion-control interactions. |
| Deferred immediate `#0` and `final` | Executable subset | The expression is evaluated at encounter. Per-process/site tickets implement last-report-wins. `#0` matures in Observed and actions run in Reactive; `final` matures in read-only Postponed. Value inputs are captured while output/ref arguments remain references. Pending tickets are flushed on process resumption and outermost process-scope disable. Disabling a labeled assertion cancels only its matching tickets in the LRM-defined logical-process scope. | Complete every subroutine argument-expression capture case and the remaining assertion-control system tasks. |
| Concurrent directive kinds | Executable subset | Bounded `assert property`, `assume property`, `cover property`, `cover sequence`, and simulation-silent `restrict property` compile to generated monitors. `expect` remains semantic IR and is rejected by executable lowering. | Add the remaining temporal forms, `expect`, and complete directive counters and controls. |
| Clock resolution and sampling regions | Executable subset | Resolved explicit or default direct edge clocks are retained. Concurrent predicates and sampled-value functions read the canonical once-per-slot Preponed snapshot; monitor bookkeeping runs in Observed. | Add clocking-event `iff`, multiple clocks and clock-flow rules, sequence/property clock arguments, global clocking, and all legal inferred-clock contexts. |
| Sampled-value functions | Executable subset | `$sampled`, `$past`, `$rose`, `$fell`, `$stable`, and `$changed` work on statically addressable packed storage. `$past` supports constant positive depth and a statically addressable gate, using bounded per-logical-process history. Concurrent predicates may select a direct alternate edge clock with an optional direct `iff`; Postponed history updates preserve strictly prior-clock semantics. The compiler coalesces referenced canonical bit ranges so Preponed capture copies only sampled state. | Add simultaneous `$past` gate plus explicit-clock `iff`, automatic/computed operands, full clock-context validation, and the remaining global sampled-value functions. |
| Boolean sequence terms | Executable subset | Four-state sampled terms are converted to assertion truth in generated SSA. | Support arbitrary legal sequence expressions, formal arguments, local variables, and match items. |
| Cycle delay `##` | Executable subset | Fixed nonnegative `##N` is compiled AOT with a total horizon of at most 63 samples. | Add ranged and unbounded delays, endpoint sets, dynamic delay extensions if supported by policy, and unbounded/end-of-simulation behavior. |
| Repetition | Executable subset | Fixed positive consecutive `[*N]` within the 63-sample horizon is compiled AOT. | Add ranges, `[*0]`, unbounded repetition, nonconsecutive `[=]`, goto `[->]`, and their endpoint semantics. |
| Sequence composition | Semantic IR; mostly rejected | Concatenation is preserved; the fixed deterministic subset is executable. `and`, `or`, `intersect`, `throughout`, `within`, and `first_match`-style match structure are represented in semantic IR. | Implement nondeterministic endpoint sets, thread merging, match-item side effects, local-variable flow, and operator-specific empty-match rules. |
| Property implication | Executable subset | `|->` and `|=>` work for an atomic antecedent and a bounded fixed consequent. Overlapping attempts use compiler-generated bitset state. False antecedents produce vacuous assertion success but never a cover hit. | Add arbitrary antecedent endpoint sets, vacuity control/accounting, followed-by forms, and composition with all property operators. |
| Other property operators | Semantic IR; rejected | IR preserves `not`, `and`, `or`, `iff`, `implies`, `until`, `s_until`, `until_with`, `s_until_with`, `nexttime`, `s_nexttime`, `always`, `s_always`, `eventually`, and `s_eventually`. | Implement weak/strong termination, finite-simulation completion, vacuity, and the precise attempt/thread semantics of each operator. |
| `strong` / `weak` | Semantic IR; rejected | Strength is preserved. | Implement end-of-simulation success/failure and composition rules. |
| `disable iff` | Executable subset | Explicit and resolved default conditions over static design state are unsampled asynchronous observers. A true condition clears live bounded attempts, prevents new attempts while it remains true, and suppresses results already queued for Reactive action in the same slot. X/Z is not true. | Support automatic operands and compose cancellation with the remaining temporal forms, assertion controls, and per-attempt local state. |
| `accept_on`, `reject_on`, `sync_accept_on`, `sync_reject_on` | Semantic IR; rejected | Abort expressions and operator kinds are preserved. | Implement asynchronous and sampled synchronous abort semantics, vacuity/result conversion, attempt teardown, and ordered action suppression. |
| Sequence/property declarations and calls | Semantic IR | Symbol references, formal metadata, defaults, actual-argument kinds, local metadata, expanded invocation bodies, default clocks, default disables, and recursive placeholders are preserved. | Execute parameterized calls, defaults, locals and recursion. Preserve a standalone declaration body when the frontend API exposes one for a declaration with required formals. |
| Local variables and sequence match items | Semantic IR; rejected | Declaration and match-item inventory is retained. | Implement per-attempt storage, initialization, flow across threads/endpoints, subroutine calls, and side-effect ordering. |
| Assertion action scheduling | Executable subset | Deferred immediate reports/actions use the LRM regions. Bounded concurrent monitors evaluate in Observed and dispatch explicit pass/fail actions or default `assert`/`assume` failures as detached Reactive callbacks. Arbitrary same-slot callback fanout is scheduler-owned, and asynchronous `disable iff` invalidates queued callbacks before their action executes. Forced native AOT remains a hybrid plan for these cold callbacks and cancellation observers; it does not relabel them as fully static work. | Add action controls, reporting/counters, `expect`, and action lifetime/capture for the remaining temporal forms. |
| Assertion control system tasks | Executable subset | Stable assertion paths/target IDs are retained for labeled immediate assertions. `$asserton`, `$assertoff`, `$assertkill`, and `$assertcontrol` actions 3/4/5 support constant levels, assertion-type and directive masks, plus global, module-instance, and exact-assertion selection. Off prevents new encounters while preserving queued deferred reports; Kill additionally cancels matching tickets. Designs without a selected control target emit no enabled-state lookup. | Add pass/fail and vacuous/nonvacuous controls, counters, dynamic/procedural selectors, concurrent-attempt control, and the remaining control actions. |
| Attempt, success, failure and vacuity accounting | Rejected except observable immediate failures | No incomplete counters are exposed. | Add counters and query/control behavior for immediate and concurrent assertions, including vacuous success and disabled/killed attempts. |
| Checker declarations and instances | Semantic IR | Checker declaration ports; static and procedural instance identities; resolved formal/actual connection metadata; instance-body declaration/parent identity, nesting and context; and procedural checker-statement identities are preserved with verifiers. An elaborated instance body retains its cloned ports, default clock/default disable, properties, assertions, procedures, and expressions. | Execute checker procedures/assertions, infer all checker clocks, implement free-variable and hierarchy behavior, and preserve a standalone uninstantiated body if the frontend exposes one; currently Slang exposes member bodies only through an elaborated `CheckerInstanceBodySymbol`. |
| Assertion VPI/API | Rejected | No partial assertion object model is exposed. | Add assertion/attempt objects, callbacks, reason/status data, controls, and stable hierarchy after core simulation semantics are complete. |

## Runtime architecture

Temporal execution is compiled ahead of time into ordinary verified simulation
SSA and compact monitor state.  The runtime performs scheduling, Preponed
snapshot reads, bounded history access, and report dispatch; it does not parse
properties or run a temporal interpreter.

Z3 is an optional **compiler-only** dependency used by constrained-random
planning; the current SVA implementation does not invoke it.  A future SVA
optimization may use it to prove or minimize a generated finite-state monitor,
but the emitted monitor must remain correct when `OBELISK_ENABLE_Z3=OFF`.
`libobelisk_rt`, generated native
executables, bytecode images, and target runtime archives must never contain or
link Z3 code.  Solver failure or an unavailable proof must only select a
conservative generated monitor, never change assertion semantics.

## Completion criteria

An item moves to **Executable** only when tests cover its syntax, four-state
sampling, overlapping attempts, vacuity/end-of-simulation behavior where
applicable, cancellation and assertion controls, region ordering, and native /
bytecode behavior at both O0 and O3.  Full SVA support additionally requires a
green `OBELISK_ENABLE_Z3=OFF` build and regression suite; compiler-side solver
tests are supplementary rather than a correctness dependency.
