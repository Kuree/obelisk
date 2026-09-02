# Obelisk host tools

Project-owned generators are built once with the native compiler. They emit an
architecture-independent `generated/` tree that native and cross builds can
consume. Add future generators to `CMakeLists.txt` with
`obelisk_add_generated_file`; do not add them to the LLVM SDK.

To build only the generators and their outputs:

```sh
cmake -S utils/host-tools -B build-host-tools -G Ninja \
  -DLLVM_DIR=/path/to/native/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/path/to/native/llvm/lib/cmake/mlir
cmake --build build-host-tools --target obelisk-generated-files
```

Pass the resulting tree to a target build:

```sh
emcmake cmake -S . -B build-wasm -G Ninja \
  -DOBELISK_GENERATED_DIR="$PWD/build-host-tools/generated" \
  <other wasm SDK options>
```

A normal native Obelisk build also stages all generated files under its own
`generated/` directory, which is how CI hands them to the wasm stage.
