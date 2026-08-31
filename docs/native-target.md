# Native target and third-party runtime inputs

Obelisk's local CMake build targets the Linux host architecture. CMake derives
the target triple from the matching compiler-rt and libc++ directories in the
pinned LLVM distribution, compiles the runtime for that triple, and stages the
static native-link inputs beside the compiler. At executable-link time Obelisk
uses clang's Driver library in-process to discover the host dynamic loader,
startup objects, `libc.so`, and `libm.so`; no compiler or linker subprocess is
run. The fixed support path is switched atomically to a content-hashed tree.

Native configuration requires the host distribution's glibc development
files. Common package names are `libc6-dev`, `glibc-devel`, and `glibc`.
Musl development files do not provide a supported native target.

Native executable generation is the default driver action:

```sh
obelisk design.sv                 # writes a.out
obelisk -c design.sv -o design.o # writes an ELF object
obelisk -emit-llvm design.sv     # writes textual LLVM IR to stdout
```

Native executables accept `--native-scheduler=auto|generic|aot`. `auto` is
the default hybrid mode: it emits a versioned static schedule for every
proven-unique actor and uses embedded bytecode only for continuation fragments
that cannot be scheduled statically. A bytecode fragment returns to AOT/native
execution at its next supported continuation boundary without replacing its
canonical frame. If no actor is statically schedulable, `auto` uses the generic
scheduler. `generic` explicitly forces that correctness oracle for the whole
design. `aot` requires every fragment to be statically schedulable and reports
the exact unsupported metadata or language feature during compilation.

The default command also bounds native code growth for generated gate
netlists. When at least 32 continuous built-in primitive actors would become
independent LLVM coroutines, auto selects compact bytecode execution for the
executable. The actors retain exact descriptor-range subscriptions, so this
does not broaden simulation wakeups. An explicit `--execution-tier=native` or
an explicitly selected native scheduler preserves the requested native path.

The generated plan is installed before the root initializer spawns any
process. It owns fixed actor slots and is checksum-coupled to an embedded
bytecode/design image when one exists. The runtime rejects duplicate slots,
stale checksums, malformed plans, and simultaneous use of one generated
mutable state block by two contexts.

Actor scheduling and actor execution tier are independent. Consequently
`--execution-tier=bytecode --native-scheduler=aot` uses the same static actor
inventory and ordering as native fragments. An immediate writable VPI deposit
whose canonical root/range has exact static fanout synchronizes the four-state
planes and directly marks the indexed AOT compute nodes ready. It does not enter
bytecode merely because the executable contains a bytecode fallback. Ambiguous
or dynamic writes, active observers or conditional waits, force/release, and
other specialization-invalidating mutations conservatively stabilize through
bytecode before returning to an indexed AOT boundary. Other unsupported
fragments are selected by their generated continuation table while supported
actors remain on the AOT schedule. A runtime action that invalidates the
installed plan still uses a validated transactional snapshot and permanently
deoptimizes to the generic path.

## Generated graph-region evaluation

The long-term AOT execution unit is a generated graph region, not a runtime
actor queue with a coarser scanning policy. The compiler partitions the
verified compute graph into input-combinational, Active/derived-trigger, and
NBA/commit regions, then orders each acyclic region into a straight-line
kernel. Convergence components become generated dirty-mask fixpoint loops.
Control loops, dynamic waits, and unsupported operations remain explicit
native-coroutine or bytecode boundaries.

Every resumable compute fragment keeps a stable fine bit. A coarse kernel owns
an ordered range of those bits and accepts a ready mask at entry. An acyclic
kernel tests each member bit, executes the selected body directly, forwards
state through SSA, and accumulates downstream member bits locally. It publishes
only final changed ranges when it reaches a kernel or event-region boundary.
This removes scheduler round trips without weakening event semantics: the fine
bits remain the canonical fracture points for duplicate-wake suppression,
bytecode-to-AOT return, exact VPI deposits, and future worker-lane placement.

State ownership is defined at those boundaries. Stable descriptors and the
VPI database identify logical objects; they do not force every intermediate
assignment through a byte-addressed runtime plane. At kernel entry, generated
code loads required value/unknown live-ins from the coherent safe-point layout.
Within the clean transaction, SSA values are authoritative. The completed
direct region form writes each final live-out once and publishes its exact
changed range; the current first form coalesces repeated writes to one
boundary accumulator stage per root. Bytecode, force/release, tracing,
reentrant calls, and ambiguous VPI operations are explicit
materialization/deoptimization boundaries. No permanent generated shadow
array is permitted.

Kernel dirtiness is an explicit generated value, not an accidental scheduler
side effect. A normal SystemVerilog process is insensitive while its body is
executing, so a union wait cannot detect transitions produced by that same
coarse body. Each generated drive or store therefore returns its exact changed
range; the kernel ORs those results into member dirty words and iterates until
the local mask is empty. This also records transient changes that later return
to their old value, which a before/after state comparison would miss. Only
boundary bits become scheduler activations after the local fixpoint. A coarse
actor that merely executes several bodies and then installs a union wait is
not a graph-region kernel and is not a legal optimization.

The first executable form specializes straight-line continuous regions of at
most 64 members. It snapshots every fixed change sensitivity in the coroutine
frame, reconstructs the fine member mask with four-state case comparisons on
wake, and tests one `i64` bit per member. Exact resolved-drive transitions OR
only later graph successors into that mask, so an acyclic region needs one
topologically ordered forward pass. The union wait is only the wake transport;
it does not select work. Initial activation sets every member bit. Edge waits,
backward dependencies, and regions larger than one leaf word remain unfused
until their generated mask forms are available.

Generated AOT bodies and fallback bodies may share outlined implementation
until the late inliner decides that duplicating a hot body is profitable. Code
unit, hierarchy, source, and VPI identities are separate immutable metadata,
analogous to debug identities surviving LLVM inlining; inlining a body never
removes its database identity. Coverage expressions are compiled into their
own generated counters and updates and do not define a bytecode scheduling
group.

Bytecode is used to stabilize only unsupported control or mutation. When
bytecode reaches a supported continuation, it transfers its exact fine ready
bits to the owning generated kernel and returns to AOT. Likewise, an immediate
VPI deposit with exact static fanout maps the written descriptor range directly
to fine bits and can enter the smallest affected AOT kernel without first
running bytecode. Ambiguous writes, conditional observers, and force/release
retain the conservative stabilization path.

Dirty indexing is hierarchical only when sparsity justifies it. Leaf words are
64-bit masks so x86 can select the next member with a single
count-trailing-zero instruction; 128-bit scalar masks require two dependent
halves, and SIMD does not improve first-set-bit selection. Optional summary
words index nonempty leaf pages for large graphs and sparse external/bytecode
ingress. Small hot native regions keep a flat leaf array, avoiding summary
maintenance on every internal transition. The compiler selects the
representation from kernel size and estimated ingress density rather than
imposing one layout globally.

Kernel materialization runs after the first verified compute graph and before
the final graph rebuild. It is followed by the simulation inliner, SROA,
mem2reg, canonicalization, CSE, simulation SCCP, and symbol DCE. The compute
graph is then rebuilt and verified once from the executable CFG; metadata-only
kernel grouping is not considered a runtime optimization.

The hybrid native pipeline freezes the bytecode image and VPI database before
applying native-only region-state rewrites. Consequently a late pass may
eliminate overwritten AOT NBA stages or form an SSA next-state epilogue while
the original fallback semantics remain encoded under the same function,
continuation, descriptor, hierarchy, and code-unit identities. Falling back is
a tier transfer at a safe point, not execution of partially rewritten bytecode.

`-c` always emits a conventional native ELF relocatable, independent of the
optimization level. Executable links select the runtime representation by
optimization level:

- `-O0` emits a native object and links `libobelisk_rt.a`.
- `-O1`, `-O2`, and `-O3` serialize the optimized generated LLVM module and
  link it together with `libobelisk_rt_lto.a` using LLD. Small unified designs
  use Full LTO; large partitioned designs use ThinLTO.

The optimized link uses matching LTO and code-generation optimization levels,
whole-program visibility, and parallel LTO backends. Broad dynamic
export is disabled; the full non-deprecated Annex I `svdpi.h` `sv*` API is
retained only when DPI is linked, for foreign objects and shared libraries.
Runtime ABI entry points that are not otherwise needed remain eligible for LTO
internalization and elimination.

`--compile-threads=<count>` controls the shared MLIR compilation pool, LLD
threading and LTO code generation. When it is
not specified, Obelisk uses LLVM's available-hardware count, clamped to at
least one. This option does not change simulator worker-lane selection;
`--threads` continues to control generated simulator lanes.

The native support tree contains three runtime archives. They are generated
from the same source revision and target flags by the pinned Clang, then
content-hashed and staged with the other link inputs:

- `libobelisk_rt.a` contains native ELF members for `-O0` and
  `-fno-lto` links.
- `libobelisk_rt_lto.a` contains unified LLVM bitcode members for Full LTO.
- `libobelisk_rt_prelinked.a` contains the runtime preoptimized with Full LTO
  for partitioned ThinLTO design links.

These are revision-coupled build-internal artifacts, not stable SDK
libraries. The LTO archive and generated bitcode are additionally coupled to
Obelisk's pinned LLVM 22.1.6 bitcode format and unified LTO pipeline. A
compiler, generated module, or runtime archive from another LLVM or Obelisk
build must not be mixed into a link.

The stable split-object, ThinLTO, and incremental-cache architecture is
described in
[Native partitioning and incremental builds](native-incremental-builds.md).

The native compiler emits generic PIE executables for the configured host
architecture. Obelisk's runtime, libc++, libc++abi, libunwind, and compiler-rt
are linked statically. The host glibc, libm, and ELF loader remain dynamic
dependencies. A generated executable therefore inherits the glibc symbol
version floor of the machine that built it; build release artifacts on the
oldest glibc system they must support. The same rule applies to Obelisk's own
Linux binary releases.

The native-support tree contains no host C-runtime binaries or distribution
package files. It retains Obelisk's license, the LLVM distribution notice, and
the complete Apache 2.0 and LLVM exception texts. LLVM components use the
Apache-2.0 license with LLVM exception; see the
[LLVM license](https://llvm.org/LICENSE.txt).

`--sysroot=<dir>` is wasm32-only. Native linking always uses the host runtime
discovered by clang, because redirecting host linker scripts through a sysroot
would make their absolute `GROUP` entries resolve against the wrong root.

Native platform code is isolated behind `TargetBackend`. CMake registers the
Linux source set: ELF input classification, clang-based host C-runtime
discovery, and LLD's ELF driver. A future macOS or Windows port should register
a parallel Mach-O or COFF source set with its own support staging and runtime
discovery; the target-independent MLIR/LLVM pipeline and driver option flow do
not need to change.
