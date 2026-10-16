# VPI queries, startup, and backdoor access

VPI is selected explicitly with `--vpi=off`, `--vpi=read`, or `--vpi=full`.
The mode controls design observability and mutation; shared libraries are
ordinary positional inputs:

```sh
obelisk --vpi=read design.sv plugin.so -o simulator
obelisk --vpi=full design.sv uvm_backdoor.so helpers.a -o simulator
```

Obelisk probes each shared object during compilation for the standard
`vlog_startup_routines` symbol. A module exporting that table is rejected under
`--vpi=off`. Probing loads the DSO and can run its ELF constructors, so native
inputs must be trusted.

At runtime every detected module is already present through `DT_NEEDED`.
Obelisk obtains a handle with `RTLD_NOLOAD`, resolves the startup table on that
specific handle, validates its ELF symbol extent and null terminator, and calls
the tables in positional-input order. Repeated filesystem identities are
deduplicated. In accordance with IEEE 1800-2017 36.10.2, startup-table routines
may only register callbacks and system tasks/functions; hierarchy and value
access begins when `cbEndOfCompile` callbacks run. Startup runs after runtime,
DPI, class, and native-state registration and before the root process is
spawned. The VPI context remains active through scheduler execution, including
DPI calls, and is deactivated before context destruction.

The current backdoor subset provides hierarchical and scoped
`vpi_handle_by_name`, `$root.` normalization, scope relations, filtered
iteration/scanning, `vpiType`, `vpiSize`, `vpiName`, `vpiFullName`, and Scalar,
Int, Vector, and BinStr values. `vpiNoDelay`, `vpiForceFlag`, and
`vpiReleaseFlag` require `--vpi=full`; driver handles are read-only. Vector
limbs use the standard 32-bit encoding with `bval = unknown` and
`aval = value XOR unknown`.

Class queries distinguish a lexical `vpiClassDefn`, a resolved specialization's
`vpiClassTypespec`, and a live `vpiClassObj` (IEEE 1800-2023 37.31–37.33).
`vpiClassTypespec` on a variable reports its declared type; on a live object it
reports the allocated type. Null class variables have `vpiObjId` zero and a
null `vpiClassObj` relation. Aliases of one live object compare equal and share
an identity. Object and automatic-member handles are weak references: they
do not keep an SV object alive, and collection invalidates them.

The class inventory includes data members, methods with explicit declarations,
effective value and type parameters, and normal constraints. Specializations
and live objects include inherited members and methods. Constraint iteration
returns only the class's own declarations in source order; inherited
constraints are reached through `vpiExtends`. A static member has one backing
storage location without an additional declaration in the physical module's
variable inventory. Lexical class handles cannot read member values, and
non-static members reached from a typespec need a live receiver for value
access. Generic lexical declarations without a realized specialization and
original parameter-default expression graphs remain incomplete.

Mailbox, semaphore, and process variables also expose `vpiClassObj` and
`vpiClassTypespec`. Upcasting a user class to its mailbox or semaphore base
preserves the outer object's identity and allocated class type. Built-in
typespecs have no `vpiClassDefn`. Process objects retain their identity after
execution finishes or is killed. Mailbox typespecs and live objects expose
their effective type parameter `T` through `vpiParameter`, preserving its
typedef spelling independently of the canonical mailbox type identity.

`vpiWaitingProcesses` on a mailbox or semaphore returns its suspended threads,
including when the resource is a built-in base of a user class. Thread handles
expose `vpiParent`, child `vpiThread` iteration, and `vpiActive`, which is true
only during that thread's execution. Thread handles expire on finish or kill;
they have `vpiAutomaticScheme` lifetime. Thread source-statement and frame
relations are still being completed.

`vpiMessages` snapshots mailbox contents in FIFO order without consuming them.
Integral and packed values retain every bit and unknown bit; real and string
values are copied. Scanned values are read-only; copied values survive removal
from the mailbox and collection of the mailbox. Class references are weak and
expire when their referents are collected; the iterator's `vpiUse` also holds
the mailbox weakly. Unpacked aggregate and container message snapshots are
still being completed.

Virtual-interface variables expose their current `vpiActual` interface or
exact modport; a null variable returns a quiet null handle. `vpiIsModPort`
describes the declared type even for a null variable. Interface modport
iteration includes unused declarations, and a modport's reverse relation is
`vpiInterface`. Virtual-interface iteration flattens fixed arrays into element
handles, while `vpiVariables` returns one array declaration. Fixed class event
arrays likewise select the event references stored in the object. Dynamic
virtual-interface arrays are still being completed.

Port handles additionally expose `vpiDirection`, `vpiPortIndex`,
`vpiPortType`, `vpiScalar`, and `vpiVector` directly from their immutable
design record. Net and variable handles expose the LRM-defined `vpiScalar` and
`vpiVector` properties from their immutable serialized type graph: unpacked
arrays recursively use their element shape, packed arrays/structs/unions are
vectors even at width one, and unpacked aggregates and real values are neither.
These queries do not inspect or materialize simulation state, register a
callback, or affect scheduler tier selection.

Handles live in a context-owned registry. `vpi_release_handle` returns their
storage immediately, and scanning an iterator through exhaustion automatically
frees the iterator as required by 38.38. Context teardown frees any handles and
callbacks the application did not explicitly release.

Error status follows IEEE 1800-2017 38.2: `vpi_chk_error` observes without
consuming the previous status, and the next call to any other VPI routine
resets it before reporting that call's result. `vpi_get64` accepts only
64-bit integer properties; ordinary integer and Boolean properties must be
queried with `vpi_get`.

Traversal uses the design database encoded into the final simulator. Scope,
module, net, and packed-storage handles wrap validated database cursors; their
stamped state offsets address the descriptor's coherent four-state value at a
scheduler safe point. Bytecode execution, language force/assign, net
resolution, and native region entry/exit use that same materialization layout.
A clean generated region may retain the value in SSA between safe points; it
must materialize before VPI can run. No separate VPI hierarchy or persistent
VPI value copy is constructed at runtime.

The reflection image also has compact, pointer-free statement and semantic
callback-site tables. A statement identity is separate from its source
location and from each site: this preserves distinct elaborated instances and
the two Table 38-6 callback points of a `for` statement. The exact cbStmt
eligibility and placement policy are generated from the VPI TableGen model.
Static inventory includes concrete statement kinds that are not cbStmt-capable;
those kinds have no callback-site records. A statement's hierarchy-scope index
identifies its exact elaborated scope. Behavioral statements also name their
owning process/function object; scope-owned continuous assignments and alias
statements use the absent-owner sentinel. Lexical statement nesting uses the
parent-statement index. Per-design VPI relation edges (such as `vpiStmt`,
`vpiElseStmt`, and ordered case/for children) are stored in a separate table
rather than being overloaded onto either field. Structural statement scope
relations are emitted automatically from the nearest scope-bearing parent
statement, a scope-capable code-unit owner, or the physical hierarchy scope, in
that order. Direct whole-source ports also carry an automatically emitted
`vpiLowConn` relation to their canonical net or variable object, including
explicitly renamed formals. Selected ports remain distinct until
select/ref-object identities are serialized and are never widened to the whole
backing object.
These immutable tables are emitted only for a VPI profile, not for VCD-only
reflection, and do not install executable probes or change the scheduler.
Production SV lowering, tier/fragment/bytecode bindings, and cbStmt dispatch are
not yet implemented.

An immediate deposit with an exact descriptor/root mapping also reuses
the generated static fanout index. After updating both four-state planes, the
runtime computes change and edge masks and marks the fanout entries' compute
nodes directly, with the packed ready bits suppressing duplicate wakes. This
skips bytecode stabilization when no observer, conditional wait, force, or
dirty specialization state is active. Deposits without an exact mapping and
force/release operations retain the guarded bytecode handoff.

A combinational DAG can propagate a deposit through its affected downstream
cone once in dependency order, provided no further input change occurs during
that propagation. A write to an internal signal starts at that signal's
consumers; it must not unconditionally replay upstream assignments and erase
the deposited value. Combining process bodies therefore has to preserve these
external activation points as well as the topological order. The experimental
statement-fusion pass currently retains the original processes in full VPI
mode until it can preserve those entry points. The existing scheduler continues
to handle deposits and force/release. These requirements follow IEEE 1800-2023
4.10 and 38.34; acyclicity alone is not an activation or override proof.

The cold lifecycle callbacks `cbEndOfCompile`, `cbStartOfSimulation`, and
`cbEndOfSimulation` are implemented, including callback-object iteration,
`vpi_get_cb_info`, release, and removal. Lifecycle registration does not
activate scheduler observation or alter native specialization. Callback input
is copied at registration, duplicate registrations remain distinct, and each
invocation receives a fresh action-callback record. `cbAfterDelay` runs at
the start of the time slot its delay selects. The synchronization callbacks
run once in that slot's region (IEEE 1800-2023 4.10 Table 4-1): `cbNBASynch`
and `cbReadWriteSynch` after Active and Inactive and before any NBA update,
and `cbReadOnlySynch` in Postponed. A synchronization callback registered for
the current slot by another one runs on the next pass over its region. Other
callback reasons, delayed writes, system task/function registration, VPI
strength and trireg access, and VPI-registered waveform dumping are not
implemented.
SystemVerilog trireg execution itself, including charge strength, retention,
decay, and connected charge sharing, is implemented independently of VPI.
Unsupported registration calls made from a startup table produce a clear
unsupported-startup failure rather than an unresolved-symbol loader crash.
Waveform dumping itself is available through the `$dump` system tasks and is
described in [Waveform dumping](waveforms.md); it reads the same design
database as the backdoor and needs no plugin.

The complete canonical `vpi_user.h`, `sv_vpi_user.h`,
`vpi_compatibility.h`, and Obelisk `svdpi.h` are staged under:

```sh
$(obelisk --print-resource-dir)/include
```
