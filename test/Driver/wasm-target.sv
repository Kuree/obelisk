// RUN: talos --target=wasm32 -O0 -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: talos --target=wasm32 -O3 -c %s -o %t.o
// RUN: %llvm_dist/bin/llvm-readobj --file-headers --sections %t.o | FileCheck %s --check-prefix=OBJECT
// RUN: talos --help | FileCheck %s --check-prefix=HELP
// RUN: not talos --target=wasm64 -emit-llvm %s -o %t.bad.ll 2>&1 | FileCheck %s --check-prefix=BAD-TARGET
//
// Exercise executable-link validation without depending on a host Emscripten
// installation. Relocating the driver lets this test provide a private target
// support tree, while deliberately incomplete sysroots reach each wasm-only
// validation boundary before lld is invoked (wasm-target-sysroot.test).

module wasm_target;
  logic [63:0] value;

  initial begin
    value = 64'h1234_5678_9abc_def0;
    #1 value++;
    $display("value=%h", value);
  end
endmodule

// LLVM: target datalayout = "e-m:e-p:32:32-{{.*}}"
// LLVM-NEXT: target triple = "wasm32-unknown-emscripten"
// LLVM: define i32 @main(
// LLVM: call i32 @talos_rt_v1_scheduler_run{{.*}}(

// OBJECT: Format: WASM
// OBJECT-NEXT: Arch: wasm32
// OBJECT-NEXT: AddressSize: 32bit
// OBJECT: Type: CODE
// OBJECT: Type: DATA
// OBJECT: Name: target_features

// HELP-DAG: -c{{ *}}Emit a relocatable object for the selected target
// HELP-DAG: -emit-llvm{{ *}}Emit textual LLVM IR for the selected target
// HELP-DAG: --sysroot=<dir>{{ *}}Use <dir> for wasm32 C and C++ link inputs
// HELP-DAG: --target=<native|wasm32>
// HELP-DAG: Select the code-generation target

// BAD-TARGET: talos: error: unsupported target 'wasm64'; expected native or wasm32
// MISSING-SYSROOT: talos: error: wasm sysroot '{{.*[\\/]}}missing' is not a directory
// WASM64-SYSROOT: has no wasm32-emscripten libraries; a wasm64 sysroot cannot satisfy the 32-bit runtime ABI
// MISSING-RUNTIME: talos: error: wasm support is missing '{{.*[\\/]}}libtalos_rt.a'
// MISSING-LIBS: talos: error: wasm sysroot is missing '{{.*[\\/]}}libstubs.a'

//--- no-runtime/bin/placeholder
// Split-file fixture: the driver executable is copied beside this file.

//--- no-runtime/lib/talos/targets/wasm32-unknown-emscripten/.complete
// Split-file fixture: target support stamp without a runtime archive.

//--- wasm64-sysroot/placeholder
// Split-file fixture: existing sysroot without wasm32 libraries.

//--- no-runtime-sysroot/lib/wasm32-emscripten/placeholder
// Split-file fixture: wasm32 library directory without target runtime support.

//--- no-libs/bin/placeholder
// Split-file fixture: the driver executable is copied beside this file.

//--- no-libs/lib/talos/targets/wasm32-unknown-emscripten/.complete
// Split-file fixture: complete target support stamp.

//--- no-libs/lib/talos/targets/wasm32-unknown-emscripten/libtalos_rt.a
// Split-file fixture: runtime presence is enough to reach sysroot validation.

//--- no-libs-sysroot/lib/wasm32-emscripten/placeholder
// Split-file fixture: wasm32 library directory without support archives.
