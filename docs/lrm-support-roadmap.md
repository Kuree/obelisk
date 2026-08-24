# IEEE 1800-2017 executable-support audit

This ledger defines the language-completeness target for Obelisk.  The target
is every executable or elaboration-affecting feature in IEEE Std 1800-2017,
except the exclusions below.  A construct is complete only when its behavior
is implemented in both native and whole-design bytecode execution where both
tiers apply.  Frontend acceptance, retained semantic IR, or a compiling design
that silently drops behavior is not executable support.

## Scope and evidence

Excluded from the completion target:

- Clause 19 functional coverage and Clause 40 code coverage;
- Clauses 36 through 39, the PLI/VPI object model, routines, and assertion API;
- VPI-dependent portions of other clauses and normative Annexes K through M;
- Clause 41, whose 2017 text is only a notice that the data-read API is
  deprecated; and
- implementation performance, including simulator worker parallelism, which
  is an engineering acceptance gate below but is not a SystemVerilog language
  feature.

Clause 35 DPI and normative Annexes H, I, and J remain in scope.  Assertion
attempt/success/failure/vacuity accounting remains in scope under Clause 16;
only the Clause 39 C API exposing that state is excluded.  Informative Annexes
D and E explicitly say their optional tasks and directives are not part of the
standard, so they are recorded separately and do not gate language
completeness.

The audit uses four evidence levels:

- **Executable**: behavior has source-to-runtime tests through both execution
  tiers, or a focused test demonstrates that the frontend-only effect changes
  elaboration as required.
- **Partial**: a useful subset is executable and one or more live boundaries
  are identified below.
- **Semantic only**: source information is retained, but executable lowering
  is absent or silently discards it.
- **Missing**: there is no usable implementation of the language feature.

The 2026-08-23 audit baseline began at commit `34e02b5c` and included the R1
`std::randomize` work then in progress, which is now commit `b97638ab`.  Fresh
external runs against that audit tree produced:

| Suite | Positive pass | Expected-error pass | Compile fail | Run fail |
| --- | ---: | ---: | ---: | ---: |
| sv-tests | 947 | 80 | 0 | 0 |
| ivtest | 1801 | 374 | 255 | 239 |
| Verilator | 1195 | 16 | 343 | 42 |

The green sv-tests run is strong evidence for its common Chapters 5-26
surface, not proof of complete clauses.  It has little or no coverage of
checkers, UDPs, specify paths, timing checks, SDF, protected envelopes, and the
missing DPI forms.  The two broader implementation suites independently expose
the live clusters named below.  The full Accellera UVM 2020.3.1 smoke also
completed its run phase at 1 ns with zero errors or fatals in both native and
whole-design bytecode execution (`-O3 -fno-lto`).  Test filenames containing
`unsupported` are not evidence by themselves. L1 renamed stale files that now
test executable net delays, wired nets, dynamic `foreach`, sampled values,
forks, event `iff`, and computed events.

## Performance gates

Language closure must preserve fast compilation and simulation. Performance is
therefore a cross-cutting acceptance gate for every implementation chunk even
though it is not itself LRM conformance:

- `benchmark/uvm/run.py` measures full Accellera UVM 2020.3.1 compilation and
  short-run startup in native and whole-design bytecode execution with
  `-O3 -fno-lto`. Its bytecode compile must remain below the existing 60-second
  absolute gate; native measurements use a 120-second timeout.
- `benchmark/scheduler/run_nba8.py` measures sustained scheduler throughput,
  scaling with cycle count and dormant waiters, runtime subscription/AOT
  counters, and native/bytecode result parity.
- Compare the median of at least three runs before and after a change on the
  same host, CPU affinity, build, flags, and compiler-thread budget whenever a
  chunk changes a compiler or runtime hot path. A median regression above 10%
  and outside the run-to-run range blocks landing until it is fixed or recorded
  with a specific explanation. Chunks off the runtime hot path still run the
  UVM smoke once in both tiers and must not change generated runtime behavior.

The 2026-08-23 audit baselines on this workspace were 34.036 seconds compile /
0.202 seconds simulate for bytecode and 76.725 seconds compile / 0.042 seconds
simulate for native. Both completed the run phase at 1 ns with zero UVM errors
or fatals. These are comparison baselines, not portable promises across hosts.

L3's three-run medians were 33.741 seconds compile / 0.202 seconds simulate for
bytecode and 75.323 seconds compile / 0.041 seconds simulate for native, with
zero UVM errors or fatals. The strict native scheduler matrix retained zero
generic candidate scans, readiness calls, and AOT fallbacks at 0, 1024, and
3072 dormant waiters through one million cycles.

L4 is off the ordinary-net runtime hot path. Its required single UVM smoke ran
in 34.590 seconds compile / 0.204 seconds simulate for bytecode and 74.951
seconds compile / 0.041 seconds simulate for native, with zero UVM errors or
fatals. Charge-strength work is gated on trireg component resolution.

L5's required single UVM smoke ran in 34.171 seconds compile / 0.203 seconds
simulate for bytecode and 76.234 seconds compile / 0.041 seconds simulate for
native, with zero UVM errors or fatals. A 4096-bit integral-power stress case
reduced native `-O0 -fno-lto` compilation from a 30-second timeout at 5.2 GB
RSS to 0.55 seconds at 117 MB RSS; native and bytecode execution both complete
in under 0.01 seconds. The full regression suite passes 1255/1255 tests.

## Clause ledger

| Clause | Level | Executable evidence and remaining work |
| --- | --- | --- |
| 3 Design and verification building blocks | Partial | Modules, programs, interfaces, packages, ordinary hierarchy, and basic configuration selection elaborate. Compilation-unit, package, module, directive, and command-line time-unit/precision precedence executes across the full legal 1 fs through 100 s scale range. Checker bodies are semantic only and UDP behavior is absent. |
| 4 Scheduling semantics | Partial | Active, Inactive, NBA, Observed, Reactive, Re-Inactive, Re-NBA, Postponed, and the Preponed snapshot hook execute through one native/bytecode scheduler. Remaining language gaps are attached to the timed constructs below. PLI callback regions are excluded with VPI. |
| 5 Lexical conventions | Executable for the audited surface | Slang supplies the lexer, preprocessor-facing tokens, literals, attributes, keywords, and identifiers. Keep this clause under differential testing, especially revision switches and literal corner cases. |
| 6 Data types | Partial | Packed 2/4-state values, real/realtime variables and nets, strings, chandles, events, enums, typedefs, parameters, casts, strengths, common net kinds, user-defined nettypes/resolution functions, typed/heterogeneous fixed-array `interconnect`, and trireg charge strength/retention/decay/sharing execute. Remaining gaps are tracked by the operator, aggregate, and container chunks below. |
| 7 Aggregate data types | Partial | Fixed arrays/structs/unions, tagged managed unions, dynamic arrays, queues, associative arrays, queries, traversal, and the registered manipulation methods execute. Remaining work includes all legal slice/reference lvalues, string range selection and character reference/NBA paths, and safe semantics for an untagged union containing a managed handle. |
| 8 Classes | Partial | Construction, inheritance, polymorphism, virtual/interface methods, parameterized classes, copying, managed properties, garbage collection, and the UVM-used surface execute. Complete the residual class/type/operator/constructor long tail exposed by focused probes and the aggregate/reference gaps shared with Clauses 6, 7, and 11. |
| 9 Processes | Partial | Structured procedures, all fork/join forms, `wait fork`, `disable fork`, timed and recursive tasks, `process` handles and control, automatic capture, and cancellation execute. Disabling a named block owned by another live process is still rejected instead of canceling only the target scope. |
| 10 Assignment statements | Partial | Blocking/NBA assignment, intra-assignment timing, common aggregate patterns, net aliasing, static continuous-assignment delays, strengths, and a restricted procedural force/assign surface execute. Complete signal-dependent force/assign reevaluation, automatic/class/unpacked/managed targets, concatenations and dynamic selects, plus the remaining queue/unpacked slice lvalues. |
| 11 Operators and expressions | Partial | Legal equality, ordering, and logical operations execute for strings, sequential containers, associative arrays, unpacked aggregates, class/chandle/process/event/virtual-interface handles, and arbitrary-width packed values. This includes handle wildcard identity equality, two-state XNOR, and compact arbitrary-width integral power with a self-determined exponent. Public `--timing=min|typ|max` selection applies to constant and dynamic selected expressions. Remaining work is concentrated in dynamic/simple range selection, string ranges, unpacked concatenation/result forms, assignment-pattern setters, and dynamic string replication. |
| 12 Procedural statements | Partial | Conditional, ordinary/pattern case, loops, jumps, `randcase`, and most `randsequence` forms execute. Recursive randsequence productions and value-returning productions still require activation frames and expression-valued production calls. |
| 13 Tasks and functions | Executable for the audited non-DPI surface | Static/automatic, recursive, virtual, class/interface, timed task, value/output/inout/ref, default argument, and cancellation behavior execute. Continue differential closure for unusual aggregate and hierarchical formal cases; DPI is tracked separately in Clause 35. |
| 14 Clocking blocks | Partial | Input/output skews, `#1step`, synchronous drives, event lists and `iff`, cycle delays, defaults, and virtual-interface clocking handles execute. Global clocking and the remaining assertion clock-inference, clock-formal, and multi-clock composition cases remain. |
| 15 Interprocess synchronization | Partial | Semaphores, typed mailboxes, named-event creation/alias/null, blocking and nonblocking trigger, `.triggered`, and `wait_order` execute in both tiers. The default untyped mailbox is rejected because the runtime currently requires one fixed element descriptor. |
| 16 Assertions | Partial | Immediate/deferred assertions and a substantial compiled concurrent subset execute. The authoritative fine-grained boundary is `docs/sva-lrm-support.md`; the implementation plan below covers accounting, full temporal composition, clocks, locals/match items, sampled values, controls, and `expect`. |
| 17 Checkers | Semantic only | Declarations, ports, resolved instances, identities, cloned bodies, clocks/disables, properties, procedures, and expressions are retained. Executable instances now receive a targeted Clause 17 diagnostic instead of being silently erased; A9 implements checker procedures, free variables, inferred clocks, assertions, hierarchy, and runtime behavior. Covergroups in checkers are excluded with coverage. |
| 18 Constrained random generation | Partial | Object streams, broad packed constraints, modes, finite domains, soft constraints, direct solve ordering, distributions, bounded `randc`, lifecycle hooks, and much of randsequence execute. The authoritative boundary is `docs/randomization-support.md`; R1-R7 below close the remaining standard surface without treating a solver resource cap as language semantics. |
| 19 Functional coverage | Excluded | Explicitly outside this project goal. |
| 20 Utility system tasks/functions | Partial | Simulation/time control, conversions, data/array queries, real math, bit-vector functions, severity, random distributions, most assertion control, and the implemented sampled functions execute. Missing normative families include `$system`, `$q_initialize`/`$q_add`/`$q_remove`/`$q_full`/`$q_exam`, the synchronous/asynchronous PLA tasks, the global-clock sampled functions, and complete assertion statistics/control behavior. |
| 21 Input/output tasks/functions | Partial | Display/write/strobe/monitor families, formatted strings, broad file I/O and scanning, read-memory, plusargs, and VCD/dumpports execute. `$writememb`/`$writememh`, the remaining scan target/reference forms, and formatting/file corner cases remain. |
| 22 Compiler directives | Executable for the audited surface | The Slang preprocessor implements the normative directive family. Directive persistence, separate-compilation-unit reset, and command-line default-timescale precedence have native/bytecode tests. Protected envelopes are a separate Clause 34 feature, not ordinary pragma acceptance. |
| 23 Modules and hierarchy | Partial | ANSI/non-ANSI modules, parameters, ports, arrays, hierarchy, bind, and common upward references elaborate. External runs retain module-library lookup, port mismatch, hierarchical path, generate-scope, and parameter-binding failures that need clause-minimal reproducers and fixes. |
| 24 Programs | Partial | Program instances and their Reactive/Re-Inactive/Re-NBA process home execute. The normative `$exit` program-control task is missing. |
| 25 Interfaces | Partial | Interfaces, modports, parameterization, interface tasks/functions, interface arrays, virtual-interface handles, calls, containers, and clocking-block access execute. Complete the residual virtual-interface clock/event/formal cases and inherit specify support from Clause 30. |
| 26 Packages | Partial | Packages, imports/exports, scope lookup, and the implemented `std` package surface, including R1 `std::randomize`, execute. Complete the remaining normative Annex G behavior through the randomization and system-task chunks. |
| 27 Generate constructs | Partial | Loop/conditional generation and ordinary external names elaborate. External tests still expose generate-scope and parameter-binding corner cases. |
| 28 Gate/switch modeling | Partial | Logic gates, buffers/inverters, tristate gates, pullup/pulldown, strengths, built-in net resolution, and static one/two/three propagation delays execute. MOS, CMOS, resistive, bidirectional pass, and controlled pass devices remain missing. |
| 29 User-defined primitives | Missing | UDP declarations and ports are imported, but table rows and sequential state semantics are not preserved, and an instance currently reaches the built-in-primitive diagnostic. |
| 30 Specify blocks | Semantic only | Specparams, timing paths, and specify blocks are imported. Executable timing paths and pulse controls now receive targeted Clause 30 diagnostics instead of being silently erased; G4 implements simple/full/edge-sensitive/state-dependent paths, delay tuples, and `showcancelled`/`noshowcancelled`. |
| 31 Timing checks | Semantic only | System timing-check nodes are imported and now receive a targeted Clause 31 diagnostic instead of being silently erased. G5 implements all stability-window and clock/control checks, edge and condition forms, notifiers, vectors, negative checks, and violation scheduling. |
| 32 SDF backannotation | Missing | `$sdf_annotate`, SDF parsing/mapping, multiple annotation, pulse limits, and delay replacement are absent. |
| 33 Configuring a design | Partial | A focused probe proves basic `design`, `default liblist`, `instance ... use`, and selecting a config as a top affect elaboration. Complete library-map files, cell/config forms, nested rules, diagnostics, and binding-report behavior; also close driver module-library lookup compatibility. |
| 34 Protected envelopes | Missing | Ordinary pragmas do not provide the standard encryption/decryption envelope flow. Implement required encodings, cipher/key/digest descriptors, key-provider integration, nested decrypted envelopes, diagnostics, and preprocessing order. |
| 35 DPI | Partial | Imported zero-time functions and synchronous tasks, C thunks, scalar/fixed-packed/string/chandle marshalling, context scope APIs, linking, and header generation execute. Exports, open/unpacked arrays and structs, all legal formal types, suspending exported-task re-entry, and disable acknowledgement are missing. `ref` is not legal on a DPI import and is therefore not a missing import feature. |
| 36-39 PLI/VPI and assertion API | Excluded | Explicitly outside this project goal. |
| 40 Code coverage | Excluded | Explicitly outside this project goal. |
| 41 Data read API | Not applicable | The 2017 clause contains no API, only a deprecation notice referring to 1800-2005. |

Normative Annex F follows the Clause 16 assertion plan.  Annex G follows the
Clause 18, 20, and 26 plan.  Annexes H, I, and J follow the Clause 35 plan.
The Annex N probabilistic distribution algorithms are implemented for the
standard `$dist_*` functions and must remain covered by deterministic runtime
tests.

## Implementation chunks

Chunks are ordered to finish common language behavior before specialized
verification, gate/timing, encryption, and foreign-interface work.  Each chunk
must cite the exact 1800-2017 clauses in its tests or implementation notes,
use minimal hand-authored MLIR for lowering tests, verify native and bytecode
parity, receive a correctness/MLIR/test review, fix the findings, and land as
one commit.

### Common language and runtime

1. **L1 — Audit hygiene and silent-drop guards, completed.** Every currently
   semantic-only executable construct now has a targeted diagnostic; stale
   negative test names and support docs are corrected.
2. **L2 — Time and min/typ/max closure (3.14, 11.11, 22.7), completed.**
   Compilation-unit, package, module, directive, and command-line
   timeunit/precision interactions execute across the full legal scale range;
   `--timing=min|typ|max` selects constant and dynamic min/typ/max expressions.
3. **L3 — Net-type closure (6.6-6.7, 10.3.3), completed.** Real/realtime nets,
   atomic user-defined nettypes and pure resolution functions, time-zero and
   Active/Reactive resolution, single inertial UDNT delays, alias chains, and
   typed or heterogeneous fixed-array `interconnect` execute in native and
   bytecode tiers. The exact Doulos 6.6.8 example also lowers successfully.
4. **L4 — Trireg charge semantics (6.6.4, 28.16, 28.16.2), completed.**
   Small/medium/large stored charge, retention, third-delay decay, connected
   charge sharing, and strength resolution execute in native and bytecode
   tiers. Charge metadata is absent from ordinary-net hot paths.
5. **L5 — Operator/type matrix closure (6.13, 6.16.1, 7.2.1, 7.4.2,
   7.5, 7.9, 7.10, 8.2, 9.7, 11.4.4-11.4.8, 15.5, 25.9), completed.**
   Legal string, sequential-container, associative-array, unpacked-aggregate,
   class/chandle/virtual-interface, process/event, and wide packed operators
   execute. Handle wildcard equality uses identity semantics, two-state XNOR
   covers both spellings, and integral power stays compact across unequal
   arbitrary operand widths in native and bytecode tiers.
6. **L6 — Select/concatenation/replication closure (10.10, 11.4-11.5).** Finish
   dynamic range selections, string ranges and dynamic string replication,
   unpacked concatenation results, and assignment-compatible conversions.
7. **L7 — Aggregate and pattern closure (7, 10.9, 11.9).** Finish legal
   assignment-pattern setters, tagged-union four-state formatting, and a safe
   policy/representation for untagged unions containing managed handles.
8. **L8 — Container/reference and untyped-mailbox closure (7.5-7.12, 13.5,
   15.4).** Complete queue and unpacked slice lvalues, escaped string-character
   references, character NBA, scan copy-out targets, heterogeneous untyped
   mailbox payloads, and differential method tests.
9. **L9 — Procedural force/assign reevaluation (10.6).** Make signal-dependent
   right-hand sides continuously reevaluate with exact dependency and release
   semantics.
10. **L10 — General force/assign targets (10.6).** Add automatic and class
    variables, unpacked/managed values, concatenations, dynamic selects, and
    user-defined nets.
11. **L11 — Cross-process scoped disable (9.6.2).** Cancel only the named
    target block and descendants when another process disables it, preserving
    task copy-out and deferred-report rules.
12. **L12 — Core frontend/lowering long-tail closure (5-13).** Reduce every
    remaining non-extension ivtest/Verilator core failure to a minimal clause
    test, then close declaration, conversion, lvalue, call, and pattern cases
    not already named above.
13. **L13 — Hierarchy, ports, and generate closure (23, 25, 27).** Fix the
    remaining legal port conversions/connections, hierarchical paths, upward
    lookup, generated scope naming, and parameter binding.
14. **L14 — Libraries, bind, and configurations (23.11, 33).** Complete module
    library search, library-map syntax, config cell/instance/config rules,
    nested selection, and binding reports.
15. **L15 — Program control (24.7).** Implement `$exit` with program-thread
    ancestry and wait-for-all-programs semantics.
16. **L16 — Global and residual clocking (14).** Implement global clocking,
    remaining virtual-interface clock events, clock arguments, and inferred
    clock contexts shared with SVA.
17. **L17 — Normative utility calls (20.16-20.18).** Implement the `$q_*`
    stochastic queue, synchronous/asynchronous PLA, and `$system` task/function
    families with exact argument and status behavior.
18. **L18 — Global sampled functions (20.13).** Implement the complete
    `$past_gclk`, `$future_gclk`, `$rising_gclk`, `$falling_gclk`,
    `$stable_gclk`, `$changed_gclk`, `$steady_gclk`, and `$changing_gclk`
    family on global-clock samples.
19. **L19 — I/O completion (21).** Implement `$writememb`/`$writememh` and
    close remaining format, scan, file-position, memory-range, plusarg, and VCD
    conformance cases.

### Randomization and `std`

20. **R1 — `std::randomize` (18.12), completed in `b97638ab`.** Direct-variable
    randomization includes initialization-stream selection for static
    initializers, shadow-safe automatic bindings, checker-only calls, and
    native/bytecode semantic tests.
21. **R2 — Runtime recursive object-graph randomization (18.4-18.7).** Replace
    static recursive-type expansion with an identity-preserving runtime walk
    that handles arbitrary finite cyclic type graphs and aliases.
22. **R3 — Complete container constraints (18.5.7).** Add multidimensional and
    fixed arrays, associative arrays, strings, element allocation/resizing, and
    all legal `foreach` forms.
23. **R4 — General constraint functions (18.5.12).** Support legal control
    flow, receivers, nested/virtual calls, system functions, dynamic selects,
    and exact implicit solve-order edges while enforcing side-effect rules.
24. **R5 — Constraint expression/domain closure (18.5).** Remove semantic
    width/cardinality implementation limits, support dynamic division/power
    guards, and preserve exact failure/distribution semantics without a hidden
    bounded-search language restriction.
25. **R6 — Distribution and randc composition (18.5.4, 18.4).** Compose
    `dist` with soft constraints, solve ordering, randc, multiple distributions,
    dynamic endpoints/weights, and arbitrary legal randc domains.
26. **R7 — Randsequence completion (18.17).** Add recursive activation frames,
    value-returning productions and rule variables, expression-valued calls,
    and the remaining legal production compositions.

### Assertions and checkers

27. **A1 — Assertion accounting (16.14, 20.12).** Add attempt, success,
    failure, vacuous-success, disabled, and killed accounting plus language-level
    controls/queries; do not implement the excluded Clause 39 API.
28. **A2 — Branching locals and match items (16.10-16.11).** Carry per-thread
    locals through every branching endpoint and add ref/output, receiver, and
    managed/dynamic match-call arguments.
29. **A3 — Empty matches and zero-delay composition (16.7-16.9).** Complete
    every legal degenerate repetition/concatenation rule and `##0` fusion.
30. **A4 — General multi-clock and clock resolution (16.13, 16.16).** Support
    immediate first terms, `##0` handoffs, maximal clocked subsequences, clock
    formals, global/default clocks, and all legal inferred contexts.
31. **A5 — Persistent and nested temporal composition (16.9, 16.12).** Remove
    fixed trace/horizon/product limits through compact thread merging and
    compose persistent branching, implication/followed-by, strength, temporal
    unary/binary operators, first-match, and nested abort/cancellation.
32. **A6 — Full procedural `expect` (16.17).** Run the complete property
    surface with locals, match items, abort/disable, implication, clocks,
    controls, cancellation, and Reactive resumption.
33. **A7 — Sampled-value completion (16.9.3, 20.13).** Add automatic and
    computed operands, full clock-context checks, and composition with A4/L18.
34. **A8 — Assertion-control completion (20.12).** Carry action snapshots
    through multi-cycle/persistent/multi-clock state, implement Kill for
    detached attempts, dynamic selectors, and controls affecting `expect`.
35. **A9 — Executable checkers (17).** Materialize checker instances,
    procedures, variables/free-variable rules, clock inference, hierarchy,
    functions, and contained assertions. Checker covergroups stay excluded.

### Gates, timing, SDF, and protected source

36. **G1 — MOS/pass/CMOS devices (28.7-28.9, 28.13-28.14).** Implement nmos,
    pmos, cmos, tran, controlled tran, resistive variants, bidirectional
    propagation, strength reduction, arrays, and delays.
37. **G2 — Combinational UDPs (29.3-29.4, 29.8).** Preserve truth-table rows
    in semantic IR and compile exact four-state matching, instances, arrays,
    strengths, and delays.
38. **G3 — Sequential UDPs (29.5-29.10).** Add state, initialization,
    level/edge tables, mixed descriptions, dominance, and scheduling.
39. **G4 — Specify paths and pulse behavior (30).** Execute all path forms,
    delay tuple selection, conditions, edge polarity/data sources, pulse
    filtering, and cancellation display controls.
40. **G5 — System timing checks (31).** Implement every standard timing check,
    conditioned/edge events, notifiers, vector expansion, negative checks, and
    violation scheduling.
41. **G6 — SDF backannotation (32).** Parse and map SDF, implement
    `$sdf_annotate`, repeated/multiple annotations, pulse limits, scale rules,
    and replacement of path/device/net delays.
42. **G7 — Protected envelopes (34).** Implement the standard preprocessing
    order, required encodings and cryptographic descriptors, key/digest
    provider interface, nested decrypted envelopes, and safe diagnostics.

### DPI-C

43. **D1 — Exported functions (35.4, 35.7).** Generate stable C entry points,
    headers, scope activation, argument/result marshalling, and native/bytecode
    call-through to zero-time SystemVerilog bodies.
44. **D2 — Exported tasks and re-entry (35.8).** Permit imported C tasks to
    call exported suspending tasks, retain simulator/process state across
    re-entry, and resume the import at the required point.
45. **D3 — Open-array ABI (35.5.6, Annexes H-I).** Implement
    `svOpenArrayHandle`, dimensions, bounds, strides, element/array pointer
    accessors, packed vector accessors, copy-in/out, and diagnostics for forms
    the LRM forbids on exports.
46. **D4 — Sized unpacked aggregates (35.5-35.8).** Marshal fixed unpacked
    arrays, unpacked structs, legal packed/unpacked nesting, and exported
    aggregate arguments with exact C layout rules.
47. **D5 — DPI type/signature closure (35.4-35.6).** Add every legal formal
    category currently missing, notably `time` and `integer`, audit function
    result restrictions, deprecated `"DPI"` spelling where applicable, and
    diagnose cross-declaration C-name/signature conflicts.
48. **D6 — DPI disable protocol (35.9).** Implement disabled-state propagation,
    `svIsDisabledState`, `svAckDisabledState`, copy-out suppression, exported
    task cancellation, and nested-call behavior.
49. **D7 — Complete `svdpi.h`/C-layer conformance (Annexes H-I).** Audit every
    required type, macro, scope/time/userdata routine, canonical header
    signature, C/C++ compatibility, and error/lifetime rule after D1-D6.

## Optional non-standard annex tail

Informative Annex D says its tasks are not part of IEEE 1800-2017.  If Obelisk
chooses compatibility beyond the conformance target, track `$countdrivers`,
`$getpattern`, `$input`, `$key`/`$nokey`, `$list`, `$log`/`$nolog`,
`$reset` and its queries, `$save`/`$restart`/`$incsave`, `$scale`, `$scope`,
`$showscopes`, `$showvars`, and `$sreadmemb`/`$sreadmemh` separately.  The
ivtest `$countdrivers` failures belong here, not in the normative Chapter 20
plan.  Annex E directives are treated the same way.

## Completion rule

The ledger is complete only when every non-excluded row is **Executable**, all
targeted unsupported diagnostics for legal forms are gone, no semantic
executable node can be silently dropped, the native/bytecode/O0/O3 and
solver-free matrices pass, and the external suite failures have either become
passes or carry a documented clause-based reason that the test is outside
IEEE 1800-2017.
