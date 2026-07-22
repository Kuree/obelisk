# Obelisk native runtime

`libobelisk_rt.a` contains target-native support code shared by generated
simulators. It is a standalone C++17 archive with a lockstep C ABI; LLVM, MLIR,
slang, and GoogleTest are not runtime dependencies.

The implementation is separated into context/buffer ownership, fragment and
bytecode dispatch, scalar formatting/display, and libc-backed file I/O
translation units. The typed compiler boundary and runtime implementation evolve
together while each runtime area remains independently testable.

## Compiler/runtime compatibility

The runtime ABI is an internal contract between components built from the same
Obelisk source revision, not a backward-compatible SDK. Updating the compiler
requires regenerating all native objects, bytecode, descriptors, and generated
drivers, then relinking them with the runtime from that same build. Loading or
linking artifacts produced by another Obelisk revision is unsupported.

The `_v1` names and `OBELISK_RT_ABI_GENERATION` identify the current schema
namespace; they do not establish a compatibility window or require a new
namespace whenever the schema changes. Layouts, enum values, and entry points
may change in place. Compile-time ABI assertions and compiler data-layout tests
detect disagreement inside one build rather than preserving old artifacts.

## Fragment ABI and bytecode

A stable descriptor handle is a kind, numeric ID, and generation; it never
contains a native address. Every process fragment consumes the same context,
frame, frame size, and continuation ID and produces one of three actions:
continue, suspend, or terminate. A fragment descriptor selects either a native
entry point or an immutable bytecode range without changing that contract.

The compact register bytecode uses fixed 16-byte little-endian instructions.
Each immutable program carries a sorted table from stable 32-bit continuation
IDs to local instruction indices, so bytecode layout never changes scheduler
identity and continuations are never truncated to bytecode program counters.
Entry lookup is logarithmic. Typed register scratch is assigned a fixed range
inside each process frame and cleared on entry, avoiding interpreter heap
allocation on every resume.
It currently provides typed 64-bit integer and boolean constants, moves,
arithmetic, bitwise operations, comparisons, bounded frame loads/stores,
branches, and fragment actions. The interpreter validates instruction size,
register types and indices, branch destinations, frame ranges, and terminal
actions. Malformed programs return `OBELISK_RT_INVALID_BYTECODE`; they do not
read or write outside the supplied process frame.

Validation covers the entire instruction encoding plus reachable control-flow
register/resource types before the first instruction executes. A malformed
instruction after a service call therefore cannot create a file or produce
display output before being rejected. Service scalar out-parameters are
zero-initialized and committed for every returned status, matching native ABI
lowering. Buffer-producing services likewise always define an interpreter-local
resource (including an empty buffer on failure); resources must be released on
every successful fragment-exit path and are automatically released on errors.
Mutable byte operands are explicit INOUT frame ranges, and a writable range in
one service site may not overlap any other operand's frame range.

A backward branch is well-formed, so validation cannot reject a fragment that
never terminates — `while (1)` is legal SystemVerilog. Differential and fuzzing
harnesses can use `obelisk_rt_v1_bytecode_execute_bounded`, which reports
`OBELISK_RT_STEP_LIMIT` after a caller-supplied instruction limit. The ordinary
production dispatch remains unbounded. Bytecode v1 now also carries validated
constant-pool and service-site metadata for format, display, and file calls.

This bytecode is the cold-fragment execution substrate, not a second scheduler.
Generated schedules and native fragments use the same action ABI and stable
handles, while dynamic runtime services remain ordinary calls through the same
lockstep C boundary.

## Four-state values

Packed logic uses two little-endian arrays of 64-bit words and an explicit bit
width. The width may be any positive value, including widths such as 5, 37, or
65 bits. In each bit position:

| unknown | value | symbol |
|---:|---:|:---|
| 0 | 0 | 0 |
| 0 | 1 | 1 |
| 1 | 0 | X |
| 1 | 1 | Z |

Only `ceil(width / 64)` words are present. Bits above the declared width in the
last word are ignored. Signed values are stored as exactly `width` bits of
two's-complement data; the signed flag affects interpretation and formatting,
not storage.

The formatter supports scalar binary, octal, decimal, hexadecimal, character,
string, real, time, scope/location, raw two-state and raw four-state, and
pattern formats. Arbitrary-width decimal conversion is performed directly on
the word array, without truncating through a host integer. Aggregate `%p` and
strength-aware `%v` remain future work.

## Files and descriptors

The runtime delegates byte and file positioning operations to libc. It adds
SystemVerilog descriptor behavior around those primitives:

- bit 0 is the stdout multichannel descriptor;
- bits 1 through 30 are combinable `$fopen(path)` output channels;
- bit 31 marks a mode-taking ordinary file descriptor;
- zero reports open failure and means "all output files" for flush.

The context owns opened streams, records per-file errors, serializes complete
writes, and closes its files during destruction. Paths and strings are always
length-delimited at the ABI boundary, so embedded NUL data is preserved for
file contents while embedded NULs in host paths and modes are rejected.

## Threading

All operations on a live context are safe to invoke concurrently. Descriptor
table changes and individual libc stream operations are linearized by the
context, display writes remain whole, and diagnostic messages are retained per
calling thread so one worker cannot overwrite another worker's `last_error`.

Context and handle lifetime remain caller-owned: create the context before
starting workers, join workers before destroying it, and do not start an
operation on a descriptor after another thread begins closing that descriptor.
Input argument storage and returned buffers must likewise remain valid and
unmodified for the duration of their owning call. Separately opened contexts do
not impose an ordering on each other's output.

## Tests

GoogleTest is fetched and added as a test-only CMake subdirectory. Runtime unit
executables are discovered by a dedicated LLVM-style
`lit.formats.GoogleTest` configuration:

```sh
ninja -C build check-obelisk-runtime-units
ninja -C build check-obelisk
```

Release-only configurations can omit all test targets and avoid fetching
GoogleTest with `-DOBELISK_INCLUDE_TESTS=OFF`.
