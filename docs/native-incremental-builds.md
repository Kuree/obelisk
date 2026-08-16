# Native partitioning and incremental builds

Native compilation uses the same stable partition boundary for parallel
code generation and incremental reuse.  Splitting one already-lowered LLVM
module by worker count is deliberately not the contract: it makes cache keys
and object membership change with the host and leaves the expensive lowering
as one monolithic step.

This contract is initially specific to the native ELF backend. The wasm64
backend continues to lower and optimize one module and links the prebuilt
wasm-object runtime. Native partition metadata is neither planned nor consumed
for wasm64 until WebAssembly partitioning has its own linkage and performance
validation; the unsplit wasm path is not inferred from native behavior.

## Partition contract

The compiler forms partitions before LLVM optimization from stable
simulation ownership:

- one primary partition owns `main`, execution metadata, state storage, and
  externally visible ABI symbols;
- functions and class methods are owned by their stable code-unit identity, so
  large UVM classes do not become indivisible native modules;
- process, task, assertion, and generated helper closures inherit that
  code-unit identity;
- a deterministic call-graph SCC closure records mutually recursive semantic
  ownership and invalidation as one dependency component; and
- generated physical helpers remain with their semantic owner.

Partition IDs are derived from semantic owners. After all cross-symbol
dependencies have been frozen, the native backend may split those owners and
SCCs into definition-level physical shards; hidden external linkage and the
ThinLTO combined index preserve their calls across shard boundaries. It
deterministically packs those units into up to two instruction-weight-balanced
physical shards per configured compile worker, capped at 256. Ordinary shards
are ThinLTO bitcode; exceptional bodies above the direct-codegen ceiling are
locally optimized native objects. Future
frontend/semantic caching will key the fine-grained semantic-owner artifacts
before this physical packing step, so a different host thread count cannot
invalidate them.

Each partition exports an explicit, deterministically named ABI surface and
imports only the symbols recorded by its dependency summary.  Module inline
assembly, aliases, ifuncs, COMDAT groups, block addresses, and any other value
whose ownership cannot be represented safely prevent partitioning until their
rules are implemented.  The unsplit path remains the correctness fallback.

## ThinLTO

Simulation-aware whole-design optimization runs before this boundary. The
partitioner therefore does not prevent scheduling, state-domain, devirtualize,
specialize, or simulation-inlining passes from seeing the complete design.
At `-O3`, ThinLTO's combined index then permits bounded cross-partition import
and inlining during native code generation. A meaningful simulation-throughput
regression relative to Full LTO is a release blocker, not an accepted cost of
incremental compilation.

Every ordinary generated native shard carries a ThinLTO summary; exceptional
direct-codegen object shards are already locally optimized. LLD builds the
combined design index, performs bounded imports, and runs independent backends
in parallel. The runtime is separately prelinked with Full LTO so its own
whole-program optimization is preserved without adding all runtime bitcode to
each design's ThinLTO index. ThinLTO replaces unified Full LTO for large native
designs; small designs may retain the single-module path when it is faster.
Wasm64 retains its existing per-module optimization and object link.

Partitioned native executables use a persistent LLD ThinLTO cache. By default
it is stored beside the output as `<output>.thinlto-cache`; builds can select a
shared build-cache location with `--thinlto-cache-dir=<dir>`. LLVM's cache key
includes the partition contents, combined-index imports, target, and codegen
configuration, so unchanged backends are reused without bypassing cross-module
optimization.

`-fno-lto` remains the explicit compile-latency choice: it performs `-O3`
optimization within each generated partition but does not request
cross-partition LLVM imports. The default `-O3` mode does not silently select
that tradeoff. Likewise, oversized process bodies are not moved to bytecode in
the default native mode merely to improve compiler latency.

The prebuilt native runtime remains a separate archive. `-fno-lto` consumes
its ordinary object archive directly. ThinLTO consumes the Full-LTO-optimized
prelinked object archive, so compiling SystemVerilog never recompiles or
assembles the runtime.

## Cache keys and invalidation

The object cache key is a digest of:

1. canonical lowered partition bitcode and its dependency summary;
2. target triple, data layout, CPU and feature set;
3. optimization, sanitizer, VPI/DPI, execution-tier, and code-generation
   options that affect the object;
4. Obelisk compiler revision, runtime ABI version, and pinned LLVM revision;
5. ThinLTO import list and imported summary/content digests.

The primary metadata partition also includes the ordered inventory of design
descriptors and partitions. Source strings retained in canonical IR—including
DPI locations and language-visible file names—therefore contribute through the
bitcode digest. Nonsemantic build-root and temporary-path prefixes are
normalized before hashing; timestamps and diagnostic options do not enter a
key. The current physical packing depends on the configured worker count and
can therefore change LLD backend-cache keys; the future semantic-owner cache
described above is intentionally worker-independent.

On a rebuild LLD recomputes the inexpensive ThinLTO combined index and reuses
backend objects for partitions whose content and import list are unchanged.
Cache entries are written atomically and are safe for concurrent readers. A
cache miss falls back to ordinary ThinLTO backend compilation without changing
generated behavior. Caching earlier frontend and semantic IR remains future
work.

## Delivery order

1. Introduce and verify deterministic partition manifests in MLIR, including
   SCC ownership, exported/imported symbols, and stable IDs.
2. Emit multiple non-LTO objects from that manifest and link them with the
   existing prebuilt runtime archive.
3. Build a Full-LTO-prelinked runtime archive and link generated partition
   bitcode through ThinLTO, retaining the unsplit fallback. (Implemented.)
4. Add the local content-addressed ThinLTO backend cache. (Implemented; opt-in
   hit/miss statistics remain future work.)
5. Cache earlier semantic and simulation IR artifacts once their serialization
   and dependency summaries are stable.

Regression coverage for the partition contract belongs in fast MLIR tests:
stable membership under reordered input, SCC co-location, deterministic
exports/imports, collision handling, and threaded-versus-serial identity.
Driver benchmarks measure cold and warm UVM compilation but are not correctness
tests.
