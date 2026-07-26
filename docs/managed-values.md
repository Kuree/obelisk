# Managed strings and containers

Obelisk's precise, non-moving heap owns both SystemVerilog class instances and
runtime-sized values. Every allocation has an out-of-line managed kind, actual
object extent, allocation extent, alignment, identity, pin count, and ticket
lock. Only objects of kind `Class` contain and validate an
`obelisk_rt_class_descriptor_v1`; runtime strings and containers use
runtime-owned descriptor tokens.

The currently implemented shared-runtime surface includes:

- immutable, non-interned tagged strings, with zero as the canonical empty
  string and inline storage for one through seven bytes;
- stable-ID registration and validation for
  `obelisk_rt_element_type_v1`;
- contiguous dynamic arrays with prefix-preserving resize;
- power-of-two queue ring buffers;
- robin-hood associative arrays with signed/unsigned integral and string keys;
- mutation-invalidated ordered associative traversal caches;
- eager recursive cloning of nested value containers, while strings and class
  handles remain shared;
- precise tracing of only live dynamic-array/queue elements and occupied
  associative key/value slots;
- managed dynamic-index reference paths that re-resolve on every access;
- native and pointer-free bytecode lowering for escaping array/queue index
  paths through the existing `ArgumentRef` ABI.

Allocation-capable operations require an active managed execution lane and are
collection points. They root owners and temporary managed values across every
allocation. Container publication validates managed element slots against the
destination context. No managed allocation or GC safepoint occurs while a
container ticket lock is held.

`obelisk_rt_managed_word_v1` and `obelisk_rt_string_v1` are 64-bit ABI words.
Tag `01` stores an explicit three-bit length and up to seven arbitrary bytes,
including embedded NULs. Tag `00` is either zero or an aligned heap-string
pointer. Inline encodings must be canonical: length zero and nonzero unused
payload bytes are rejected. Tags `10` and `11` are invalid. Heap pointers are
checked against the exact live-object registry before any header access, so
aligned garbage and stale or cross-context handles are rejected safely. Heap
strings store only a runtime
descriptor and relaxed-atomic cached hash in their header; their explicit
length comes from the collector's recorded allocation extent, and a trailing
NUL supports C ABI consumers without changing SystemVerilog length semantics.

`obelisk_rt_v1_string_view` replaces the old raw-byte contract. Callers supply
eight bytes of scratch, which receives inline bytes; a heap view points into
the immutable rooted allocation. `obelisk_rt_v1_string_concat_many` validates
and roots every source, computes the exact result extent, and performs at most
one managed allocation. Empty, single-input, and SSO results allocate nothing.

Trace entries declare whether a managed slot contains a class, string, or
container word. Publication checks that category before exposing a new value.
Managed-root records scan tagged words: inline strings have no outgoing edge,
while heap strings are context-validated and marked.

Simulation IR has one-word managed types for strings, dynamic arrays, queues,
and associative arrays. Bytecode metadata distinguishes strings from native
managed-object pointers so an SSO word is never interpreted as a host pointer.
Layout and state encoding treat managed types before recursively inspecting
their element types, so (for example) a dynamic array of `logic` remains one
managed root rather than acquiring value and unknown planes.

Full source-language string and container lowering, container methods and
`foreach`, null-owner lvalue writeback, associative/string-character
reference-path lowering, managed NBAs, wide/wildcard and class keys, and
compiler-emitted literal/element-descriptor startup remain staged. Unsupported
forms retain explicit diagnostics instead of silently dropping behavior.
