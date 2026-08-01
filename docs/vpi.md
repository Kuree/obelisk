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
deduplicated. Startup runs after runtime, DPI, class, and native-state
registration and before the root process is spawned. The VPI context remains
active through scheduler execution, including DPI calls, and is deactivated
before context destruction.

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

An immediate deposit with an exact descriptor/root mapping also reuses
the generated static fanout index. After updating both four-state planes, the
runtime computes change and edge masks and marks the fanout entries' compute
nodes directly, with the packed ready bits suppressing duplicate wakes. This
skips bytecode stabilization when no observer, conditional wait, force, or
dirty specialization state is active. Deposits without an exact mapping and
force/release operations retain the guarded bytecode handoff.

Callbacks, delayed writes, system task/function registration, strengths,
trireg behavior, and waveform dumping are not implemented. Registration calls
made from a startup table produce a clear unsupported-startup failure rather
than an unresolved-symbol loader crash.

The complete canonical `vpi_user.h`, `sv_vpi_user.h`,
`vpi_compatibility.h`, and Obelisk `svdpi.h` are staged under:

```sh
$(obelisk --print-resource-dir)/include
```
