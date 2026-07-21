# Obelisk native runtime

`libobelisk_rt.a` contains target-native support code shared by generated
simulators. It is a standalone C++17 archive with a versioned C ABI; LLVM,
MLIR, slang, and GoogleTest are not runtime dependencies.

The implementation is separated into context/buffer ownership, scalar
formatting/display, and libc-backed file I/O translation units. This keeps the
public ABI independent from the compiler while allowing each runtime area to
evolve independently.

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
