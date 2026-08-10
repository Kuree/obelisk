# Waveform dumping

`$dumpfile`, `$dumpvars`, `$dumpoff`, `$dumpon`, `$dumpall`, `$dumplimit`, and
`$dumpflush` write a VCD file. Tracing needs the design database, so a design
must be built with `--vpi=read` or higher; the names, scopes, widths, and
canonical bit offsets in the dump all come from that image rather than from a
second hierarchy built at run time.

```sh
obelisk --vpi=read design.sv -o simulator
```

## Collection is a per-slot difference, not a callback

Values are collected by differencing the canonical four-state planes once per
time slot, immediately before each scheduler time advance. No writer is
instrumented: it does not matter whether a bit was published by the generic
scheduler, a bytecode intrinsic, a generated direct-state store, or a VPI
deposit, because the difference observes only the settled result of the slot.
That is also exactly what VCD requires — one record per variable per time step,
carrying the final value — so intra-slot glitches never reach the file.

The traced set is coalesced into byte-aligned windows over the state planes.
Each slot compares a window with `memcmp` against a shadow that mirrors the
live plane at the same bit offsets, and descends to individual variables only
where a window moved. The shadow is a mirror rather than an extracted copy, so
the comparison needs no bit shifting.

## Aliasing

IEEE 1364 permits several `$var` declarations to share one identifier code when
the variables always hold the same value. Declarations addressing the identical
canonical range qualify unconditionally — they are the same storage — so they
collapse to one traced range with several `$var` lines. The duplicate is
removed from both the file and the per-slot difference.

Nets that are merely connected are not folded, even though resolution keeps
them equal at every settle point. Deciding that requires connectivity metadata
that only the bytecode tier carries, and one design must not dump differently
per execution tier.

## Aggregates

A packed vector, packed array, or packed struct is one value and is dumped as
one vector. An unpacked array is not a value: each element is an independent
signal, so it is declared per element (`mem[0]`, `mem[1]`, …) at its own
canonical offset, and only the elements that moved produce records. Nested
unpacked dimensions expand in the same way. Element ordinal zero occupies the
lowest canonical bits and carries the leftmost declared index.

VCD has no aggregate form, so an array too large to expand is omitted rather
than degraded into some other shape: a single wide vector would be well-formed
VCD that silently presents many independent signals as one value. The default
bound is 32 elements, matching the trade-off Verilator settled on, since the
cost of an expanded array is paid per element per time slot in both file size
and scan time. Omissions are named on stderr and the bound is configurable:

```sh
OBELISK_RT_DUMP_MAX_ARRAY=4096 ./simulator
```

Real variables are declared `$var real` and emitted as `r` records rather than
as the bit pattern of their storage.

## Interaction with execution tiers

Generated `run_until` for a proven free-running clock owns the scheduler clock
directly and completes whole time slots without re-entering the runtime, which
would leave the difference with nothing to observe. A design containing
waveform calls therefore does not use that tier. This is decided during
compilation rather than by deoptimizing part way through a run.

Every other tier produces the same file: the traced set and its records come
from the design image, not from how the design executes.

## Limits

`$dumpfile` requires a string literal. The traced set and the file are fixed
for the whole run, so a computed name buys nothing that a literal does not, and
a literal keeps managed strings out of every execution tier.

A `$dumpvars` scope that does not name a design scope is reported once on
stderr and no waveform is written. `$dumplimit` stops writing once the file
reaches the given size; the file remains valid VCD up to that point.
