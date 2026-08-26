# Container builds

## `Dockerfile.llvm-wasm`

Builds LLVM + MLIR + LLD 22.1.6 for **wasm**, producing the CMake package
layout Obelisk's top-level `CMakeLists.txt` already validates. This is the
dependency the in-browser compiler needs; there is no upstream prebuilt
LLVM/MLIR wasm distribution to consume.

The SDK is wasm32 because no Safari release implements the Memory64 proposal;
a wasm64 module cannot load on any iPhone or Mac.

The image carries **two** LLVM distributions, because a cross build needs host
tools and those can never be the wasm build's own binaries:

| Path | Arch | Origin | Provides |
| --- | --- | --- | --- |
| `/opt/llvm-native` | x86-64 | official release archive + source-built generators | `clang++`, `llvm-ar`, `llvm-ranlib`, `llvm-tblgen`, `mlir-tblgen`, `llvm-min-tblgen` |
| `/opt/llvm-wasm32` | wasm32 | built here | MLIR + LLD libraries and CMake packages |

No clang is *built* — the native one comes prebuilt in the official archive at
no build cost, and the wasm side is LLVM + MLIR + LLD only.

Toolchain image (can build either a native or a wasm Obelisk):

```sh
docker build -f docker/Dockerfile.llvm-wasm \
  -t obelisk-llvm-wasm32 docker/
```

Tarball of just the SDK:

```sh
DOCKER_BUILDKIT=1 docker build -f docker/Dockerfile.llvm-wasm \
  --target artifact --output type=local,dest=./dist docker/
```

Output:

- `dist/llvm-mlir-lld-wasm32.tar.xz`
- `dist/llvm-mlir-lld-wasm32.tar.xz.sha256`

Consume it through the existing prebuilt-archive path, no source change needed:

```sh
cmake -S . -B build-wasm -G Ninja \
  -DOBELISK_LLVM_PREBUILT_URL=file://$PWD/dist/llvm-mlir-lld-wasm32.tar.xz \
  -DOBELISK_LLVM_PREBUILT_SHA256=$(cut -d' ' -f1 dist/llvm-mlir-lld-wasm32.tar.xz.sha256)
```

### CI image

CI builds this image from the checked-in Dockerfile and caches the resulting
BuildKit layers. This avoids a hidden dependency on the visibility or lifetime
of a separately published GHCR package. A local image can still be built with:

```sh
docker build --target toolchain -f docker/Dockerfile.llvm-wasm \
  -t obelisk-llvm-wasm32:local docker/
```

The image provides:

| Variable | Meaning |
| --- | --- |
| `OBELISK_LLVM_WASM_SDK` | Path to the SDK tarball, for `OBELISK_LLVM_PREBUILT_URL` as a `file://` URL |
| `OBELISK_LLVM_WASM_SHA256_FILE` | File holding its bare checksum, for `OBELISK_LLVM_PREBUILT_SHA256` |

### Why these settings

| Setting | Reason |
| --- | --- |
| wasm32 (no `-sMEMORY64`) | The only width Safari runs; Memory64 has shipped in no Safari release. |
| `-fexceptions` | Not `-fwasm-exceptions`. Native wasm exceptions tie the module to one Safari generation — 26 implements exnref, earlier releases only the superseded encoding. LLVM is built without exceptions but `libLLVMSupport` can still contribute exception opcodes, so CI disassembles the SDK and final modules. |
| `LLVM_TARGETS_TO_BUILD=WebAssembly` | The in-browser compiler only ever emits wasm. Drops every other backend. |
| `LLVM_ENABLE_THREADS=OFF` | Simulations do not need pthreads, so the page avoids SharedArrayBuffer and therefore the COOP/COEP headers that static hosts cannot set. |
| `MinSizeRel` / `-Oz` | Free: designs are compiled to a *separate* module at `-O3` at runtime, so shrinking this SDK costs no simulation throughput — only compile latency. |
| Native TableGen stage | The official binary release lacks `llvm-min-tblgen`, so the generators are built from the same source rather than mixing distributions. |
| Native tools copied into `bin/` | TableGen tools make the SDK drop-in compatible. Everything is copied by explicit name — globbing `/opt/llvm-native/bin/*` would pull in all 172 binaries (8.9 GB). |

The wasm target runtime is compiled ahead of time by Emscripten into ordinary
wasm32 objects. Those are built `-fno-exceptions`, so the simulation module
carries no exception opcodes and no JavaScript trampolines, and are archived
as `libobelisk_rt.a`; the in-browser linker does not consume runtime LLVM
bitcode. The matching native `clang++` currently carried in the SDK is not used
for this runtime build.

### Build cost

Roughly 15 minutes of compilation on a 16-core machine — fast because only the
WebAssembly target is built, with no clang, tools, tests or examples. The
native TableGen stage adds ~90 s. Peak memory scales with
`--build-arg JOBS=<n>`.

Packaging uses `xz -T8 -3`. The default single-threaded `-6` took longer than
the entire LLVM build; see the comment in the Dockerfile for the measurements.
