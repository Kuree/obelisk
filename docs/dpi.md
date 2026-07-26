# DPI-C imports

Obelisk supports zero-time DPI-C imported functions and synchronous tasks.
The native and embedded-bytecode execution tiers enter the same validated
runtime boundary and invoke the same generated C thunk, so marshalling,
context functions, errors, and copy-outs are shared.

Supported arguments and function results are `byte`, `shortint`, `int`,
`longint`, scalar `bit` and `logic`, enums with one of those canonical base
types, and fixed packed 2-state or 4-state values. Formal directions may be
`input`, `output`, or `inout`. Renamed C identifiers, `pure`, and `context`
imports are preserved. Fixed packed aggregates, including packed structs and
unions, use the standard bit-vector or logic-vector representation.

Open and unpacked arrays, strings, `real`/`shortreal`, `chandle`, `ref`, DPI
exports, task suspension, and disable acknowledgement are not supported yet.
They produce diagnostics instead of falling back to a different ABI. The
execution bytecode now reserves distinct string, binary32, and binary64
register categories; this is not yet a promise that DPI marshalling accepts
`const char *`, `float`, or `double`.

## Build an implementation

Print the resource directory and generate prototypes from the elaborated
imports:

```sh
RESOURCE_DIR=$(obelisk --print-resource-dir)
obelisk --emit-dpi-header design.sv -o design_dpi.h
```

The resource include directory is `$RESOURCE_DIR/include` and contains the
pinned `svdpi.h`.

Compile C or C++ for Obelisk's x86-64 Linux target, then pass the resulting
object to the final link:

```sh
cc -c dpi.c -I"$RESOURCE_DIR/include" -o dpi.o
obelisk design.sv dpi.o -o simulator

c++ -c dpi.cpp -I"$RESOURCE_DIR/include" -o dpi.o
obelisk design.sv dpi.o -o simulator
```

The generated header has `extern "C"` guards, so a C++ implementation should
include it rather than redeclare the imports.

Static archives and shared libraries are ordinary repeatable linker inputs:

```sh
ar rcs libdpi.a dpi.o
obelisk design.sv libdpi.a -o simulator

cc -shared -o libdpi.so dpi.pic.o
obelisk design.sv libdpi.so -o simulator
```

Direct inputs are classified by contents rather than filename suffix. ELF
objects, archives, LLVM bitcode, and ELF shared objects are passed to the
native link; text remains SystemVerilog input. Native inputs are rejected for
`-c`, textual LLVM output, and non-link actions because those artifacts can be
linked by the caller.

Every shared object is retained as a normal `DT_NEEDED` dependency under
`--no-as-needed`. Obelisk never copies it. The supplied directory is added to
the simulator's `DT_RUNPATH`: relative inputs use a literal `$ORIGIN`-relative
entry and absolute inputs use their normalized absolute parent directory.
SONAMEs become runtime loader identities. A no-SONAME library is linked by
basename so an absolute build path is not recorded in `DT_NEEDED`. Duplicate
loader identities and SONAMEs containing `/` are rejected.

During compilation Obelisk loads each shared object to validate it and probes
its module handle for `vlog_startup_routines`. This can execute ELF
constructors, so native inputs must be trusted. A shared object without that
symbol is an ordinary DPI/helper dependency. A VPI startup object requires
`--vpi=read` or `--vpi=full`.

Native DPI objects, static archives, and shared libraries remain supported at
every optimization level. They are ordinary native linker inputs and do not
participate in the `-O1` through `-O3` Full-LTO optimization of generated code
and the Obelisk runtime.

A DPI input may instead contain LLVM bitcode compatible with Obelisk's pinned
LLVM 22.1.6 unified Full-LTO pipeline. Such input participates in the same
Full-LTO link and may be optimized with generated and runtime code. Bitcode is
a build-internal compatibility surface, not a portable DPI distribution
format. LLD reports incompatible LLVM bitcode directly; Obelisk never silently
falls back to native or non-LTO linking.

## Select the execution tier

Native execution is the default:

```sh
obelisk design.sv dpi.o -o simulator
```

To embed and require the bytecode interpreter:

```sh
obelisk --execution-tier=bytecode design.sv \
  dpi.o -o simulator
```

Bytecode contains stable import and scope IDs, source coordinates, and typed
register references only. The generated native wrapper still contains and
registers the C thunks before the root process is spawned.

## Context functions

The supplied `svdpi.h` exposes scope get/set and lookup, scope names, per-scope
user data, caller file and line, simulation time, time unit, and time
precision. Scope handles are stable for the lifetime of one runtime context.
Nested calls restore the previous thread-local active scope. A nonzero C
return from an imported task reports the dedicated unsupported-disable status.
Both execution tiers stop the current process before committing copy-outs or
executing later statements. Exported-task re-entry and disable acknowledgement
remain deferred.
