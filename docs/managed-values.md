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

Class descriptors also carry an optional randomization layout. Edge records
identify each direct strong `rand` class-handle field and the exact
`rand_mode` word and bit that control it. Variable records describe direct
packed `rand` and `randc` instance fields, including width, signedness,
four-state planes, mode state, and `randc` permutation storage. Static random
properties and runtime-sized containers remain separate plan inputs rather
than being misrepresented as object-relative scalar slots. The runtime follows
base descriptors before derived descriptors, skips null and disabled edges,
and discovers the recursive object set in deterministic breadth-first order.
Discovery deduplicates stable heap identities, pins the resulting objects, and
flattens their active packed variables in object and declaration order while
the compiler/runtime bridge composes one simultaneous solver plan.

Simulation IR has one-word managed types for strings, dynamic arrays, queues,
and associative arrays. Bytecode metadata distinguishes strings from native
managed-object pointers so an SSO word is never interpreted as a host pointer.
Layout and state encoding treat managed types before recursively inspecting
their element types, so (for example) a dynamic array of `logic` remains one
managed root rather than acquiring value and unknown planes.

Fixed structs and arrays recursively carry precise managed-root offsets.
Unpacked tagged unions do as well: their internal payload gives each arm a
separate aligned slot, keeps inactive managed slots at the canonical null
value, and stores the tag after that disjoint payload. This representation is
shared by native and bytecode execution and is intentionally independent of
the source union's overlapping syntax. An untagged union containing a managed
handle remains unsupported because it has no discriminator with which precise
tracing can distinguish pointer bits from an inactive ordinary member; that
case is diagnosed during class and process-storage layout instead of being
traced conservatively.

Source `string` values use this representation throughout semantic constants,
design and procedural storage, fixed aggregates, ports, parameters, captures,
and subroutine arguments and results. The native and bytecode tiers share the
same byte semantics for literals, packed conversion, concatenation and
replication, comparison, indexing and character update, substring and case
conversion, every standard numeric conversion method, conditional and case
selection, delayed assignments, managed `%s` formatting, dynamic display
formats, managed file paths and modes, and `$fgets` string destinations.
Whole-string stores compare byte contents before publishing a change, rather
than comparing allocation handles.

DPI-C input, output, inout, and function-result strings use the standard
`const char *`/`const char **` ABI and copy C results into simulator-owned
managed storage. VPI string visibility remains intentionally excluded and
diagnoses its unsupported boundary. String scanning (`$sscanf` and `$fscanf`),
escaping string-character `ref` aliases, and nonblocking character-path
updates also retain explicit diagnostics. These require scanner target records
or reference paths that preserve partial assignment and exact alias semantics
across both execution tiers.

Source dynamic arrays execute allocation and resize, value-copy assignment,
indexing and reference formals, equality, `foreach`, assignment patterns,
aggregate elements, array queries, sensitivity, and the implemented reduction,
locator, ordering, uniqueness, mapping, reverse, and shuffle methods.
Associative arrays execute signed and unsigned integral keys up to 64 bits and
string keys, hidden defaults, indexing, deletion, equality, value-copy and
reference semantics, deterministic traversal, mixed-container `foreach`, array
queries, and every array method registered by the current Slang frontend.

Queues have a validated managed ring-buffer representation and are used for
array-method result values, but the complete source queue surface is not yet
executable. Queue-specific insertion/removal methods and unsupported source
forms receive diagnostics rather than being silently discarded. DPI-C and VPI
marshalling of every managed container also remains outside the current
boundary.
