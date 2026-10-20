# DPI-C

Obelisk supports DPI-C imported functions and tasks plus scope-specific
exported functions and suspending tasks.
The native and embedded-bytecode execution tiers enter the same validated
runtime boundary and invoke the same generated C thunk, so marshalling,
context functions, errors, and copy-outs are shared.

Supported formals are `byte`, `shortint`, `int`, `longint`, four-state
`integer`, four-state 64-bit `time`, `shortreal`, `real`/`realtime`, scalar
`bit` and `logic`, enums with a canonical integral base type, fixed packed
2-state or 4-state values, `string`, and `chandle`. Formal directions may be
`input`, `output`, or `inout`. Function results support the subset permitted by
35.5.4; in particular, `integer` and `time` are valid formals but invalid
function result types. IEEE binary32 `shortreal` maps directly to C `float`;
binary64 `real` and `realtime` map directly to C `double`. Four-state
`integer` and `time` use `svLogicVecVal`, preserving X and Z in every
direction. Renamed C identifiers, `pure`, and `context` imports are preserved,
and the optional pre-standard `"DPI"` spelling is rejected; use normative
`"DPI-C"`. Fixed packed aggregates, including packed structs and unions, use
the standard bit-vector or logic-vector representation. Fixed, dynamic, queue,
and empty open-array shapes use `svOpenArrayHandle`; sized unpacked arrays and
unpacked structs use exact generated C layouts. Compact layout plans remain
explicit in Simulation MLIR and are shared by native and bytecode lowering.

Following IEEE 1800-2017 H.7.4 and H.8.10, an input string uses `const char *`
and an output or inout string uses `const char **`; `chandle` uses `void *`
and the corresponding extra pointer level for copy-out formals. Input string
storage remains simulator-owned and is valid only for the call. Returned and
copy-out C strings must be valid initialized null-terminated addresses and
are copied immediately into simulator-owned managed strings. Neither side
frees the other side's string storage.

Generated headers include prototypes and concrete aggregate layouts for
imports, exported functions, and exported tasks. Simulation IR freezes one
checked signature, aggregate/open-array layout, and exact scope record per
elaborated clone. Generated C entry points select that scope and dispatch
through native, hybrid, or validated bytecode descriptors. Exported tasks can
suspend and re-enter the scheduler. The Clause 35.9 disable/acknowledgement
protocol propagates across nested calls and suppresses copy-out on disable.
`ref` imports and open-array exports are rejected as required by the LRM.

Pristine Slang v11 rejects a small set of otherwise legal queue and mixed
fixed/dynamic open-array source bindings before Obelisk receives an AST. Those
cases remain explicit source-level XFAILs; the Simulation MLIR and runtime
shape paths are tested independently without patching Slang.

## Build an implementation

Print the resource directory and generate prototypes from the elaborated DPI
imports and exports:

```sh
RESOURCE_DIR=$(obelisk --print-resource-dir)
obelisk --emit-dpi-header design.sv -o design_dpi.h
```

The resource include directory is `$RESOURCE_DIR/include` and contains the
pinned `svdpi.h`.

On Linux, compile C or C++ for the same host architecture and ABI as Obelisk,
then pass the resulting object to the final link:

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

Annex J discovery is also available. `-sv_root` establishes the relative
root, `-sv_liblist` reads an indented `#!SV_LIBRARIES` bootstrap list, and
`-sv_lib name` adds `name.so` on Linux or `name.dll` on Windows. Bootstrap
libraries are ordered before direct libraries and aliases of the same file
are suppressed.

Direct inputs are classified by contents rather than filename suffix. ELF or COFF
objects, archives, LLVM bitcode, and native shared libraries are passed to the
native link; text remains SystemVerilog input. Native inputs are rejected for
`-c`, textual LLVM output, and non-link actions because those artifacts can be
linked by the caller.

On Linux, every shared object is retained as a normal `DT_NEEDED` dependency
under `--no-as-needed`. Obelisk never copies it. The supplied directory is added to
the simulator's `DT_RUNPATH`: relative inputs use a literal `$ORIGIN`-relative
entry and absolute inputs use their normalized absolute parent directory.
SONAMEs become runtime loader identities. A no-SONAME library is linked by
basename so an absolute build path is not recorded in `DT_NEEDED`. Duplicate
loader identities and SONAMEs containing `/` are rejected.

On Linux, Obelisk loads each shared object during compilation to validate it
and probes its module handle for `vlog_startup_routines`. This can execute ELF
constructors, so native inputs must be trusted. A shared object without that
symbol is an ordinary DPI/helper dependency. A VPI startup object requires
`--vpi=read` or `--vpi=full`.

Native DPI objects, static archives, and shared libraries remain supported at
every optimization level. They are ordinary native linker inputs and do not
participate in the optional Full-LTO optimization of generated code and the
Obelisk runtime enabled by `-flto` at `-O1` through `-O3`.

A DPI input may instead contain LLVM bitcode compatible with Obelisk's pinned
LLVM 22.1.6 unified Full-LTO pipeline. With `-flto`, such input participates in
the same Full-LTO link and may be optimized with generated and runtime code.
Bitcode is a build-internal compatibility surface, not a portable DPI distribution
format. LLD reports incompatible LLVM bitcode directly; Obelisk never silently
falls back to native or non-LTO linking.

The wasm32 target has no host C ABI. It rejects DPI and Annex J inputs
explicitly before probing or loading foreign libraries.

### Windows DLLs

Windows supports COFF objects, static archives, matching SDK bitcode, and DLLs
in both native and bytecode execution. Build foreign code with the SDK's Clang
and `-fms-runtime-lib=dll`. For statically linked foreign objects, define
`DPI_DLLISPEC` and `PLI_DLLISPEC` as empty so calls use ordinary external symbols.

A DLL calling `sv*`, `vpi_*`, or exported SystemVerilog functions imports them
from the final simulator executable. Generate its host import library with the
SDK's `llvm-dlltool` and the staged standard API export list. This example uses
the automatically downloaded x86-64 SDK; an ARM64 SDK uses `-m arm64` and its
own target triple in the support path:

```powershell
$sdk = "$PWD/build-windows/sdk/Library/bin"
$resource = & ./build-windows/tools/driver/obelisk.exe --print-resource-dir
& "$sdk/llvm-dlltool.exe" -m i386:x86-64 -D simulator.exe `
  -d "$resource/targets/x86_64-pc-windows-msvc/obelisk-pi.def" -l host.lib
& "$sdk/clang.exe" -fms-runtime-lib=dll -fuse-ld=lld -shared dpi.c `
  "-I$resource/include" host.lib -Xlinker /def:dpi.def -o dpi.dll
./build-windows/tools/driver/obelisk.exe design.sv dpi.dll -o simulator.exe
./simulator.exe
```

Here `dpi.def` lists the DLL's exported foreign functions under `EXPORTS`.
For VPI, also export `vlog_startup_routines DATA` and enable `--vpi=read` or
`--vpi=full`. Additional exported SystemVerilog entry points can be listed in a
second `.def` file and imported with another `llvm-dlltool` invocation.
The `-D` image name must match the final executable basename, including `.exe`.
Obelisk reads DLL exports and imports with LLVM, generates its DLL import
libraries in process, and exports the host functions requested by the DLLs.
It keeps each DLL loaded even when it provides only a VPI startup table.

Direct DLL inputs are copied beside the final executable and recorded by
basename. Keep those DLLs beside the simulator when deploying it; the bundle
can move together while preserving the executable name used by host imports.
Distinct DLLs with the same case-insensitive basename are rejected. DLLs are
inspected without loading them into the compiler process.
At startup, the Windows runtime resolves each VPI table in its already-loaded
module and bounds its null-terminated table by readable mapped storage.

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

Bytecode contains stable import and scope IDs, source coordinates, typed
register references, and compact aggregate/open-array plans. The generated
native wrapper still contains and registers the C thunks before the root
process is spawned.

## Context functions

The supplied `svdpi.h` follows the non-deprecated IEEE 1800-2017 Annex I
surface: canonical packed-vector macros and helpers, the complete open-array
query/access family, scope get/set and lookup, scope names, per-scope user
data, caller file and line, and disable state. The optional deprecated SV3.1a
tail is omitted. Scope handles are stable for the lifetime of one runtime
context, and nested calls restore the previous thread-local active scope.

Non-context imports use a distinct lowering and runtime entry point. They emit
no caller-file or scope metadata and perform no scope lookup, source-string
allocation, active-call construction, context transaction, or thread-local
write. Context imports retain those services. For tasks, return status zero is
normal completion and status one is the disable protocol; any other status is
fatal. Native and bytecode tiers suppress copy-out when disable propagates.
