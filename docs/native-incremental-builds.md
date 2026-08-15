# Native partitioning and incremental builds

Native compilation will use the same stable partition boundary for parallel
code generation and incremental reuse.  Splitting one already-lowered LLVM
module by worker count is deliberately not the contract: it makes cache keys
and object membership change with the host and leaves the expensive lowering
as one monolithic step.

## Partition contract

The compiler will form partitions before LLVM optimization from stable
simulation ownership:

- one primary partition owns `main`, execution metadata, state storage, and
  externally visible ABI symbols;
- class methods are grouped by semantic class identity;
- process, task, assertion, and generated helper closures are grouped by their
  stable code-unit identity;
- a deterministic call-graph SCC closure prevents mutually recursive bodies
  from being split, even when an SCC is oversized; and
- oversized non-SCC owner groups are divided by stable symbol hash, never by
  input order, worker count, or current machine load.

Partition IDs are derived from semantic owner and stable symbol hashes.  The
number of parallel workers only controls how many ready partitions compile at
once.  Adding an unrelated function therefore does not renumber or move
existing partitions.

Each partition exports an explicit, deterministically named ABI surface and
imports only the symbols recorded by its dependency summary.  Module inline
assembly, aliases, ifuncs, COMDAT groups, block addresses, and any other value
whose ownership cannot be represented safely prevent partitioning until their
rules are implemented.  The unsplit path remains the correctness fallback.

## ThinLTO

Every generated partition and target-runtime archive member will carry a
ThinLTO summary.  LLD will build the combined index, perform bounded imports,
and run independent backends in parallel.  ThinLTO replaces Full LTO for large
generated designs; small designs may retain the single-module path when it is
faster.

The prebuilt native runtime remains a separate archive.  `-fno-lto` consumes
its ordinary object archive directly.  ThinLTO consumes a prebuilt ThinLTO
archive, so compiling SystemVerilog never recompiles or assembles the runtime.

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
normalized before hashing; timestamps, worker count, and diagnostic options do
not enter a key.

On a rebuild the compiler reuses unchanged frontend/library artifacts, lowers
only changed semantic owners and their invalidated dependents, recomputes the
ThinLTO index, and recompiles only partitions whose own content or import list
changed.  Cache entries are written atomically and are safe for concurrent
readers.  A cache miss or incompatible manifest falls back to ordinary
compilation without changing generated behavior.

## Delivery order

1. Introduce and verify deterministic partition manifests in MLIR, including
   SCC ownership, exported/imported symbols, and stable IDs.
2. Emit multiple non-LTO objects from that manifest and link them with the
   existing prebuilt runtime archive.
3. Build a ThinLTO runtime archive and link generated partition bitcode through
   ThinLTO, retaining the unsplit fallback.
4. Add the local content-addressed object/index cache and expose hit/miss
   statistics through opt-in compiler diagnostics.
5. Cache earlier semantic and simulation IR artifacts once their serialization
   and dependency summaries are stable.

Regression coverage for the partition contract belongs in fast MLIR tests:
stable membership under reordered input, SCC co-location, deterministic
exports/imports, collision handling, and threaded-versus-serial identity.
Driver benchmarks measure cold and warm UVM compilation but are not correctness
tests.
