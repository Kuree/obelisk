# Obelisk semantic IR

The `obelisk` dialect is the explicit contract between CIRCT Moore and LLVM.
Moore resolves SystemVerilog source constructs; this dialect preserves their
runtime meaning; later passes choose layouts, runtime calls, coroutines, and
machine code.

## Representation boundary

- `!obelisk.logic<W>` is an exact four-state packed value. Its canonical
  lowering has two W-bit planes: `unknown=0` selects 0/1 from `value`, while
  `unknown=1` selects X/Z from `value`.
- Builtin MLIR integers represent proven or source-declared two-state packed
  values. Four-state to two-state conversion is always explicit.
- `!obelisk.ref<T>` is mutable variable/field storage.
  `!obelisk.net<T>` is a resolved, potentially multi-driver net. They are not
  interchangeable.
- Process, event, object, container, synchronization, random-stream,
  constraint, chandle, and VPI types are opaque semantic handles. Their ABI
  layout is chosen only during lowering.
- Simulation time is normalized to design precision before entering
  `!obelisk.time`; it is never host `time_t`.

## Operation families

The dialect defines first-class operations for:

1. exact four-state arithmetic, comparison, reduction, concatenation, slicing,
   conditional merging, and explicit two-state conversion;
2. globals, automatic variables, nets, drivers, force/release, and sampled
   nonblocking assignments;
3. all 17 IEEE 1800 event regions, processes, spawn/join/kill, delay/event/edge
   suspension, and named events;
4. class descriptors, fields/methods, object lifetime, dynamic dispatch, and
   boxing;
5. dynamic arrays, associative arrays, mailboxes, and semaphores;
6. deterministic hierarchical PRNG streams, state save/restore, `randc`,
   compiled constraints, and atomic randomization;
7. assertions, coverage sampling, DPI tasks, VPI access/callbacks, diagnostics,
   system effects, termination, and tracing.

Compatible CIRCT `sim` operations remain available in the same module for
dynamic strings, value-semantics queues, formatting, ordinary DPI functions,
plusargs, and file primitives. Obelisk does not duplicate them merely to change
the dialect prefix. A lowering must translate them through the same runtime ABI
and may use them only where their documented semantics match IEEE behavior.

## Verification policy

Structural invariants use ODS constraints (`AllTypesMatch`,
`TypesMatchWith`, and constrained result types) whenever expressible. Focused
C++ verifiers remain only for parameter arithmetic such as constant-plane
widths, concatenated widths, and slice bounds. Unknown or effectful operations
are never marked pure, which keeps optimization conservative until explicit
memory/effect interfaces are added.

All operations have declarative assembly formats. Quoted generic operation
syntax is a debugging escape hatch, not the persisted or tested format.
