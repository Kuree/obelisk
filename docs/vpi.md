# VPI startup and backdoor access

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

Handles live in a context-owned arena. Releasing a handle marks it dead without
reusing its record, so double release and exhausted iterators are diagnosed
deterministically.

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
that order.
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

The cold lifecycle callbacks `cbEndOfCompile`, `cbStartOfSimulation`, and
`cbEndOfSimulation` are implemented, including callback-object iteration,
`vpi_get_cb_info`, release, and removal. Lifecycle registration does not
activate scheduler observation or alter native specialization. Callback input
is copied at registration, duplicate registrations remain distinct, and each
invocation receives a fresh action-callback record. Other callback reasons,
delayed writes, system task/function registration, VPI strength and trireg
access, and VPI-registered waveform dumping are not implemented.
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
