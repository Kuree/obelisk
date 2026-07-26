# Managed strings and containers

Obelisk's precise, non-moving heap owns both SystemVerilog class instances and
runtime-sized values. Every allocation has an out-of-line managed kind, actual
object extent, allocation extent, alignment, identity, pin count, and ticket
lock. Only objects of kind `Class` contain and validate an
`obelisk_rt_class_descriptor_v1`; runtime strings and containers use
runtime-owned descriptor tokens.

The currently implemented shared-runtime surface includes:

- immutable, non-interned strings, with null as the canonical empty string;
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

`obelisk_rt_v1_string_bytes` returns immutable bytes and a length. The pointer
is valid only while the string owner remains rooted. Empty strings return the
static C string `""`; no external pointer is interpreted as a heap object.

Simulation IR has one-word managed types for strings, dynamic arrays, queues,
and associative arrays. Layout and bytecode state encoding treat these types
before recursively inspecting their element types, so (for example) a dynamic
array of `logic` remains one managed root rather than acquiring value and
unknown planes.

Full source-language container methods and `foreach`, null-owner lvalue
writeback, associative reference-path lowering, container NBAs, wide/wildcard
and class keys, and compiler-emitted element-descriptor startup remain staged.
Unsupported forms retain explicit diagnostics instead of silently dropping
behavior.
