# Obelisk semantic IR

The `obelisk` dialect is the complete target of the elaborated Slang dialect.
It preserves source semantics while later passes choose storage layouts,
runtime calls, process representations, and machine code.

```sh
obelisk -emit-obelisk -I path/to/includes -DNAME=value input.sv

obelisk -emit-slang input.sv |
  obelisk-opt --convert-slang-to-obelisk
```

## Representation boundary

- `!obelisk.logic<W>` is an exact four-state packed value. Its eventual
  lowering uses separate value and unknown planes.
- Builtin MLIR integers represent source-declared or proven two-state values.
  Four-state to two-state conversion is explicit.
- Packed and unpacked arrays, structs, and unions remain distinct. Field names
  and types are held directly by Obelisk aggregate types.
- Strings, real and shortreal values, queues, enumerations, virtual interfaces,
  subroutine signatures, source ranges, declared integral ranges, and
  signedness have distinct concrete types.
- `!obelisk.ref<T>` is variable or field storage; `!obelisk.net<T>` is resolved,
  potentially multi-driver net storage. Selection operations preserve that
  distinction.
- Process, event, object, container, synchronization, random-stream,
  constraint, chandle, and VPI values remain semantic handles until runtime
  lowering.
- Simulation time is normalized to design precision before entering
  `!obelisk.time`.

## High-level operations

Every supported slang semantic kind has a concrete `obelisk.sv.*` operation.
This includes definitions, hierarchy, ports, parameters, generates, class and
subroutine constructs, expressions and assignments, control flow and timing,
constraints and randomization, assertions and coverage, primitives, continuous
assignments, and specify constructs.

These operations are distinct registered C++ classes. They are not a generic
operation containing a string or enum opcode. A shared ODS base provides common
source identity and location fields without erasing the concrete kind.

The Slang-to-Obelisk pass has a strongly typed conversion pattern for every
inventory entry and a recursive concrete type converter. It marks the full
Slang dialect illegal. Successful conversion therefore contains no Slang
operations or types and cannot silently retain an unsupported source node.

## Runtime-oriented operations

The dialect also defines first-class lower-level operations for:

1. four-state arithmetic, comparison, reduction, concatenation, slicing, and
   conditional merging;
2. globals, automatic variables, nets, drivers, force/release, and nonblocking
   assignments;
3. IEEE 1800 event regions, process spawn/join/kill, delays, event and edge
   suspension, and named events;
4. class descriptors, fields, methods, lifetime, dispatch, and boxing;
5. dynamic and associative containers, mailboxes, and semaphores;
6. hierarchical PRNG streams, `randc`, compiled constraints, and atomic
   randomization;
7. assertions, coverage sampling, DPI tasks, VPI access and callbacks,
   diagnostics, termination, and tracing.

Rewriting a high-level operation to these runtime-oriented operations is a
separate, semantics-preserving lowering step.

## Verification

ODS constraints express structural invariants where possible. Focused native
verifiers enforce width, range, field, and slice arithmetic that cannot be
expressed declaratively. Operations with unknown effects are kept conservative
until an explicit effect interface describes them.

Persistent syntax uses custom assembly. Generic quoted MLIR syntax is only a
debugging escape hatch.
