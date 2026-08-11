# Container builds

## `Dockerfile.llvm-wasm64`

Builds LLVM + MLIR + LLD 22.1.6 for **wasm64**, producing the CMake package
layout Obelisk's top-level `CMakeLists.txt` already validates. This is the
dependency the in-browser compiler needs; there is no upstream prebuilt
LLVM/MLIR wasm distribution to consume.

The image carries **two** LLVM distributions, because a cross build needs host
tools and those can never be the wasm build's own binaries:

| Path | Arch | Origin | Provides |
| --- | --- | --- | --- |
| `/opt/llvm-native` | x86-64 | official release archive + source-built generators | `clang++`, `llvm-ar`, `llvm-ranlib`, `llvm-tblgen`, `mlir-tblgen`, `llvm-min-tblgen` |
| `/opt/llvm-wasm64` | wasm64 | built here | MLIR + LLD libraries and CMake packages |

No clang is *built* — the native one comes prebuilt in the official archive at
no build cost, and the wasm64 side is LLVM + MLIR + LLD only.

Toolchain image (can build either a native or a wasm64 Obelisk):

```sh
docker build -f docker/Dockerfile.llvm-wasm64 -t obelisk-llvm-wasm64 docker/
```

Tarball of just the wasm64 SDK:

```sh
DOCKER_BUILDKIT=1 docker build -f docker/Dockerfile.llvm-wasm64 \
  --target artifact --output type=local,dest=./dist docker/
```

Output:

- `dist/llvm-mlir-lld-wasm64.tar.xz`
- `dist/llvm-mlir-lld-wasm64.tar.xz.sha256`

Consume it through the existing prebuilt-archive path, no source change needed:

```sh
cmake -S . -B build-wasm -G Ninja \
  -DOBELISK_LLVM_PREBUILT_URL=file://$PWD/dist/llvm-mlir-lld-wasm64.tar.xz \
  -DOBELISK_LLVM_PREBUILT_SHA256=$(cut -d' ' -f1 dist/llvm-mlir-lld-wasm64.tar.xz.sha256)
```

### Publishing for CI

CI does not build this image. The SDK only changes when the pinned LLVM
version does, and building it on a 4-vCPU hosted runner would add roughly
3.7 core-hours to every run. Build and push it locally instead; the
`wasm-build-and-test` job runs inside it via `container:`.

```sh
docker build --target toolchain -f docker/Dockerfile.llvm-wasm64 \
  -t ghcr.io/kuree/obelisk-llvm-wasm64:22.1.6-2 \
  -t ghcr.io/kuree/obelisk-llvm-wasm64:latest docker/

docker push ghcr.io/kuree/obelisk-llvm-wasm64:22.1.6-2
docker push ghcr.io/kuree/obelisk-llvm-wasm64:latest
```

Pushing needs a token with `write:packages`; the token from `gh auth login`
does not carry that scope by default.

**Tagging.** `<llvm version>-<revision>`. The LLVM version alone does not
identify the contents — the same 22.1.6 has produced very different images as
the Dockerfile changed. Bump the revision when this file changes, the version
when the LLVM pin moves, and pin the exact tag in `.github/workflows/ci.yml`
rather than tracking `latest`, so a rebuild can never silently change what CI
runs against.

**Make the package public**, or CI cannot pull it: GHCR packages default to
private. On the package page, Package settings → Change visibility → Public.
Link it to the repository from the same page so it appears under the repo.

The image provides:

| Variable | Meaning |
| --- | --- |
| `OBELISK_LLVM_WASM64_SDK` | Path to the SDK tarball, for `OBELISK_LLVM_PREBUILT_URL` as a `file://` URL |
| `OBELISK_LLVM_WASM64_SHA256_FILE` | File holding its bare checksum, for `OBELISK_LLVM_PREBUILT_SHA256` |

### Why these settings

| Setting | Reason |
| --- | --- |
| `-sMEMORY64=1` | Mandatory. `runtime/lib/ABI.cpp` asserts `sizeof(void*) == 8` and every descriptor layout assertion depends on it. wasm32 needs an ABI redesign. |
| `LLVM_TARGETS_TO_BUILD=WebAssembly` | The in-browser compiler only ever emits wasm. Drops every other backend. |
| `LLVM_ENABLE_THREADS=OFF` | Simulations do not need pthreads, so the page avoids SharedArrayBuffer and therefore the COOP/COEP headers that static hosts cannot set. |
| `MinSizeRel` / `-Oz` | Free: designs are compiled to a *separate* module at `-O3` at runtime, so shrinking this SDK costs no simulation throughput — only compile latency. |
| Native TableGen stage | The official binary release lacks `llvm-min-tblgen`, so the generators are built from the same source rather than mixing distributions. |
| Native tools copied into `bin/` | TableGen tools make the SDK drop-in compatible. Everything is copied by explicit name — globbing `/opt/llvm-native/bin/*` would pull in all 172 binaries (8.9 GB). |

The wasm target runtime is compiled ahead of time by Emscripten into ordinary
wasm64 objects. Those objects use Emscripten's exception ABI and are archived
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
