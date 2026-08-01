# Native target and third-party runtime inputs

Obelisk's local CMake build provisions one hermetic native target:
`x86_64-unknown-linux-gnu` with glibc 2.28. The first build downloads and
verifies the pinned Debian 10 packages, assembles a compilation sysroot below
the build directory, and stages only later link inputs beside the compiler.
The content-addressed completion stamp makes ordinary subsequent builds reuse
the verified archives, extracted packages, and sysroot. A changed package pin,
staging layout, or pre-extracted sysroot content selects a new versioned tree;
the fixed target path is switched to that completed tree atomically.

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

`-c` always emits a conventional native ELF relocatable, independent of the
optimization level. Executable links select the runtime representation by
optimization level:

- `-O0` emits a native object and links `libobelisk_rt.a`.
- `-O1`, `-O2`, and `-O3` serialize the optimized generated LLVM module and
  link it together with `libobelisk_rt_lto.a` using LLD Full LTO.

The optimized link uses matching LTO and code-generation optimization levels,
whole-program visibility, and parallel Full-LTO partitions. Broad dynamic
export is disabled; only the `sv*` DPI context API is retained for foreign
objects and shared libraries. Runtime ABI entry points that are not otherwise
needed remain eligible for LTO internalization and elimination.

`--compile-threads=<count>` controls the shared MLIR compilation pool, LLD
threading, and the number of Full-LTO code-generation partitions. When it is
not specified, Obelisk uses LLVM's available-hardware count, clamped to at
least one. This option does not change simulator worker-lane selection;
`--threads` continues to control generated simulator lanes.

The native support tree contains both runtime archives. They are generated
from the same source revision and target flags by the pinned Clang, then
content-hashed and staged with the other link inputs:

- `libobelisk_rt.a` contains native x86-64 ELF members for `-O0`.
- `libobelisk_rt_lto.a` contains LLVM bitcode members for `-O1` through `-O3`.

These are revision-coupled build-internal artifacts, not stable SDK
libraries. The LTO archive and generated bitcode are additionally coupled to
Obelisk's pinned LLVM 22.1.6 bitcode format and unified Full-LTO pipeline. A
compiler, generated module, or runtime archive from another LLVM or Obelisk
build must not be mixed into a link.

The native compiler emits generic x86-64 PIE executables. Obelisk's runtime,
libc++, libc++abi, libunwind, and compiler-rt are linked statically. glibc,
libm, libpthread, libdl, librt, and the ELF loader remain dynamic dependencies.
Consequently, anyone redistributing an executable generated by Obelisk must
deploy it to a compatible glibc system (or provide a compatible glibc runtime)
and comply with the terms applicable to that deployment. Generated programs
do not contain the staged glibc shared objects.

The local target uses these exact packages:

- `libc6_2.28-10+deb10u4_amd64.deb`, SHA-256
  `80b59743f7b47f0644d211d56918851404e66ab7fbba17e60e90664c12fc5822`
- `libc6-dev_2.28-10+deb10u4_amd64.deb`, SHA-256
  `d759a8102b932dc51e3a25b8cc3b91f3718adda774b0c42b431117662b4750cc`
- `linux-libc-dev_4.19.316-1_amd64.deb`, SHA-256
  `fde95d52b753e7b9b372d22b5c7154b31bea3b9d63239ef0c40655da94c141a3`

Package copyright notices, the complete LGPL 2.1 and GPL 2.0 texts, and the
complete Apache 2.0 and LLVM exception texts are retained in the local support
tree. The LLVM distribution's license notice is retained there as well. glibc
is primarily LGPL-2.1-or-later; its shared
library mechanism does not apply the LGPL to Obelisk or to generated programs.
Linux UAPI headers are compilation inputs only and are omitted from the staged
linker support. LLVM components use the Apache-2.0 license with LLVM exception.
See the [GNU LGPL 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html),
[Linux licensing rules](https://www.kernel.org/doc/html/v4.17/process/license-rules.html),
and [LLVM license](https://llvm.org/LICENSE.txt).

This mechanism is for local source builds. It intentionally provides no
target that packages or redistributes the compiler together with Debian's
glibc files. Any future binary compiler distribution needs a separate
compliance change that supplies the exact matching Debian source, complete
license texts, notices, modification information, and required build scripts.
The matching glibc source artifacts are published beside the binaries in the
[Debian security archive](https://archive.debian.org/debian-archive/debian-security/pool/updates/main/g/glibc/).
This document is implementation guidance, not a substitute for legal review.

For offline builds, set `OBELISK_TARGET_PACKAGE_CACHE` to a directory
containing the three verified `.deb` files; set the CMake cache entries
`OBELISK_TARGET_LIBC6_URL`, `OBELISK_TARGET_LIBC6_DEV_URL`, and
`OBELISK_TARGET_LINUX_LIBC_DEV_URL` to `file://` URLs; or set
`OBELISK_TARGET_SYSROOT_DIR` to a pre-extracted sysroot. Pre-extracted inputs
are keyed by their file contents and symlink targets rather than by directory
name, so replacing files at the same path provisions a new sysroot.
At compiler invocation time, `--sysroot=<dir>` replaces only the glibc link
inputs; missing or escaping paths are errors and never fall back to host files.
