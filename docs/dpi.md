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

Open and unpacked arrays, strings, real types, `chandle`, `ref`, DPI exports,
task suspension, and disable acknowledgement are not supported yet. They
produce diagnostics instead of falling back to a different ABI.

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
obelisk design.sv --dpi-link=dpi.o -o simulator

c++ -c dpi.cpp -I"$RESOURCE_DIR/include" -o dpi.o
obelisk design.sv --dpi-link=dpi.o -o simulator
```

The generated header has `extern "C"` guards, so a C++ implementation should
include it rather than redeclare the imports.

Static archives and shared libraries are ordinary repeatable linker inputs:

```sh
ar rcs libdpi.a dpi.o
obelisk design.sv --dpi-link=libdpi.a -o simulator

cc -shared -o libdpi.so dpi.pic.o
obelisk design.sv --dpi-link=libdpi.so -o simulator
```

Shared objects retain normal ELF `DT_NEEDED`, SONAME, and loader search-path
behavior. Configure the deployment's library path in the usual way; Obelisk
does not add an RPATH or call `dlopen`. Missing imported C symbols are strong
linker errors. `--dpi-link` is intentionally rejected for `-c`, textual LLVM
output, and non-link actions because those artifacts can be linked by the
caller.

## Select the execution tier

Native execution is the default:

```sh
obelisk design.sv --dpi-link=dpi.o -o simulator
```

To embed and require the bytecode interpreter:

```sh
obelisk --execution-tier=bytecode design.sv \
  --dpi-link=dpi.o -o simulator
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
