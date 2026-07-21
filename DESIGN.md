# Obelisk frontend design

Obelisk compiles SystemVerilog through a semantic, elaborated boundary:

```text
SystemVerilog
    │  slang v11.0 parser, name resolution, type checking, elaboration
    ▼
Slang MLIR dialect (`slang.*`, `!slang.*`)
    │  exhaustive typed conversion
    ▼
Obelisk MLIR dialect (`obelisk.sv.*`, `!obelisk.*`)
    │  simulation and runtime lowering
    ▼
LLVM / executable simulator
```

The frontend walks slang's semantic AST directly. It does not serialize the
AST to JSON and does not route source through an intermediate HDL dialect. This
is important for UVM: constraints, randomization, classes, assertions,
sequences, coverage, timing controls, generated hierarchy, and parameterized
instances are semantic constructs that a synthesis-oriented source IR cannot
represent completely.

## Why the frontend starts from scratch

The previous frontend passed SystemVerilog through CIRCT's Moore dialect. That
path is useful for hardware-oriented compilation, but it is not a complete UVM
semantic boundary. Moore does not represent the full elaborated class model,
constraint and randomization system, virtual dispatch, assertion and sequence
semantics, functional coverage, or the other verification constructs on which
UVM depends.

Once those semantics have been omitted or flattened, a later conversion cannot
reconstruct them. Adding more Moore-to-Obelisk conversion patterns would
therefore preserve only the subset that survived the earlier boundary. Obelisk
instead starts again at slang's elaborated semantic AST and defines its own
complete source and target dialects. This is a frontend replacement, not an
incremental extension of the old pipeline: CIRCT is neither a build dependency
nor an intermediate representation in the new architecture.

## Toolchain and dependencies

Obelisk consumes the official LLVM 22.1.6 binary distribution. It uses only
LLVM and MLIR's public CMake packages, headers, tools, and libraries; LLVM and
MLIR are not built as part of Obelisk. On a normal configure, CMake downloads a
checksummed platform archive into `build/_downloads` and extracts the SDK into
`build/llvm-mlir`. The toolchain is therefore local to, and removable with, the
build tree:

```sh
cmake -S . -B build -G Ninja

# Fully offline: use a previously downloaded official archive. It is still
# extracted inside build/llvm-mlir.
cmake -S . -B build -G Ninja \
  -DOBELISK_LLVM_PREBUILT_URL=file:///archives/LLVM-22.1.6-Linux-X64.tar.xz
```

The selected SDK includes `llvm-tblgen`, `mlir-tblgen`, and the LLVM and MLIR
CMake exports. The official archive does not ship the `FileCheck` or `not`
test executables, so Obelisk builds small compatible drivers against the
prebuilt `LLVMFileCheck` and `LLVMSupport` libraries. The archive SHA-256 is
checked before extraction, and all LLVM and MLIR libraries come from the same
distribution.

The current latest slang release is v11.0. CMake downloads its checksummed
source archive and builds the library as part of Obelisk. A command-line slang
binary is not sufficient because the importer uses the C++ semantic AST API.
Official release binaries are therefore useful as standalone compilers, but
not as an Obelisk SDK dependency.

For reproducible or offline builds, extract the exact v11.0 source archive and
configure with:

```sh
cmake -S . -B build -G Ninja \
  -DOBELISK_SLANG_SOURCE_DIR=/src/slang-11.0
```

CMake verifies downloads by SHA-256 and disables slang's tools, tests,
documentation, Python bindings, benchmarks, and install rules. Only the
slang-facing Obelisk frontend is compiled as C++20; the rest of Obelisk stays
at the LLVM SDK's C++17 baseline.

## Slang semantic boundary

The `slang` dialect is intentionally textual and inspectable. Its C++ namespace
is `obelisk::slangir`. Each concrete semantic dispatch kind in slang v11.0 has
one concrete registered operation. The inventory covers 220 kinds across:

- symbols and semantic types;
- statements and timing controls;
- expressions and assignment patterns;
- constraints and random-sequence productions;
- assertion, sequence, and property expressions;
- coverage-bin selection expressions.

The importer is an exhaustive `slang::ast::ASTVisitor` with one explicit
overload per inventory entry and explicit rejection of invalid sentinel nodes.
There is no catch-all visitor overload. CMake extracts the dispatch cases from
the release's `ASTVisitor.h` and compares them with
`SlangASTNodes.def`; an upstream addition, removal, or rename fails
configuration until the dialect, importer, target operations, conversion, and
tests are updated together.

Compilation definitions are imported from slang's sorted definition inventory,
including uninstantiated modules, interfaces, programs, and primitives. The
semantic root is then visited to retain compilation units, packages, class
definitions, and the elaborated instance and generate hierarchy. Anonymous
nodes receive traversal-derived IDs, and symbol paths come from slang's
resolved hierarchy.

Four-state constants are stored without conversion through host integers.
Semantic types retain width, signedness, state domain, declared ranges,
packedness, array bounds, queue bounds, associative wildcard indices, class and
covergroup identities, virtual-interface modports, and task/function
signatures. File ranges and macro expansion locations become MLIR locations
and explicit range metadata.

`obelisk -emit-slang` stops after this boundary. Compilation diagnostics or an
invalid semantic node prevent IR emission.

## Obelisk completeness boundary

Obelisk has concrete high-level operations corresponding to every Slang
semantic operation. They live under `obelisk.sv.*`; there is no generic
payload operation and no semantic-kind opcode enum.
Existing lower-level Obelisk operations are reused only when their types,
effects, regions, and scheduling semantics match the source construct.

The conversion uses strongly typed
`OpConversionPattern<obelisk::slangir::...>` instances generated from the same
checked inventory. Its type converter maps every Slang type to a concrete
Obelisk type. The entire Slang dialect is illegal in the conversion target, so
success guarantees that no `slang.*` operation or `!slang.*` type remains.
Partial conversion and silently discarded regions fail. Operations from other
dialects remain legal only when their values, regions, and nested attributes
contain no Slang types.

This high-level completeness is separate from runtime lowering. Conversion to
Obelisk means the SystemVerilog meaning is represented in the target dialect;
it does not claim that every construct has already been lowered to LLVM.

## Driver and tools

`obelisk` owns its frontend option model and maps it explicitly onto
`slang::driver::Driver`. Third-party driver option structures do not cross the
frontend API.

```sh
# Default: parse, elaborate, import, and completely convert.
obelisk design.sv
obelisk -emit-obelisk design.sv

# Stop at the elaborated source boundary.
obelisk -emit-slang design.sv

# Inspect or convert persisted source IR.
obelisk -emit-slang design.sv | obelisk-opt
obelisk -emit-slang design.sv |
  obelisk-opt --convert-slang-to-obelisk
```

When multiple output-action flags are present, the last flag wins. The driver
supports include paths, system include paths, macro definitions and removals,
command files, library paths/extensions/files, single compilation units,
library macro inheritance, selected tops, parameter overrides, language
revision, timescale, warning control, and direct advanced slang arguments.

`obelisk-opt` registers the Slang and Obelisk dialects and the conversion pass.
There is no separate source-import translation executable.

## Verification and testing

All persistent syntax uses custom assembly. Declarative constraints express
fixed type and region relationships, with native verifiers for invariants such
as integral widths/ranges and aggregate consistency.

The lit suite covers:

- source and target custom-assembly round trips;
- verifier rejection for invalid source and target types and operations;
- the exact AST and conversion inventories;
- frontend flag behavior and output-action precedence;
- fast compilation of the checked-in mock-UVM fixture;
- opt-in compilation of unmodified Accellera UVM under IEEE 1800-2017 and
  IEEE 1800-2023.

Run lit directly; CTest is intentionally not part of the test flow:

```sh
ninja -C build
lit -sv build/test

# If lit is not on PATH:
env/bin/lit -sv build/test
```

A successful source-to-target regression round-trips emitted Slang assembly,
runs the complete conversion, verifies the result, and checks that no Slang
entity survives.
