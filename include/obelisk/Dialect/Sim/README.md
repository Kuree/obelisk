# Obelisk semantic IR

The `obelisk` dialect is the explicit contract between CIRCT Moore and LLVM.
Moore resolves SystemVerilog source constructs; this dialect preserves their
runtime meaning; later passes choose layouts, runtime calls, coroutines, and
machine code.

The current conversion is an optimizer pass:

```sh
obelisk-translate input.sv |
  obelisk-opt --convert-moore-to-obelisk
```

`obelisk-translate` only changes the input representation from SystemVerilog
source to Moore IR. The conversion itself stays in `obelisk-opt`, where it can
participate in normal pass pipelines.

## Representation boundary

- `!obelisk.logic<W>` is an exact four-state packed value. Its canonical
  lowering has two W-bit planes: `unknown=0` selects 0/1 from `value`, while
  `unknown=1` selects X/Z from `value`.
- Builtin MLIR integers represent proven or source-declared two-state packed
  values. Four-state to two-state conversion is always explicit.
- Packed and unpacked fixed arrays, structs, and unions remain distinct
  Obelisk types. Named fields use nested HW struct/union inventories, but the
  SystemVerilog packedness is not discarded before ABI/layout lowering.
- `!obelisk.void` is source-level SystemVerilog void; it is not MLIR `none`.
- `!obelisk.ref<T>` is mutable variable/field storage.
  `!obelisk.net<T>` is a resolved, potentially multi-driver net. They are not
  interchangeable. Constant, dynamic, and concatenated lvalue selection has
  separate `ref.*` and `net.*` operations so selection never erases resolution
  semantics.
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

The pass has a compile-time inventory of all 238 operations in the pinned
Moore API. Common operations lower directly to typed Obelisk, Arith, HW, CF,
Func, or Sim operations. Every remaining operation lowers through a strongly
typed `OpConversionPattern<SourceOp>` to a semantic operation carrying a
generated `SemanticKind` enum. There is no runtime operation-name dispatch or
string opcode. The enum encoding records the legal operation family and fixed
operand, result, and region arities; target-only verifiers reject a kind placed
in the wrong family or with the wrong shape. Source-specific attributes live in
a declared dictionary property, with opcode-specific validation for enum
metadata. Module/fork graph regions, CFG regions, terminators, symbol isolation,
and class symbol-table scopes remain structurally distinct. The pass also
checks its typed inventory against every operation registered by the pinned
Moore dialect. A successful full conversion therefore contains no Moore
operations or types, without falsely claiming that all high-level semantics
have already reached LLVM.

## Verification policy

Structural invariants use ODS constraints (`AllTypesMatch`,
`TypesMatchWith`, and constrained result types) whenever expressible. Focused
C++ verifiers remain only for parameter arithmetic such as constant-plane
widths, concatenated widths, and slice bounds. Unknown or effectful operations
are never marked pure, which keeps optimization conservative until explicit
memory/effect interfaces are added.

All operations have declarative assembly formats. Quoted generic operation
syntax is a debugging escape hatch, not the persisted or tested format.
