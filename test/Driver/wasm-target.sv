// RUN: obelisk --target=wasm64 -O0 -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk --target=wasm64 -O3 -c %s -o %t.o
// RUN: %llvm_dist/bin/llvm-readobj --file-headers --sections %t.o \
// RUN:   | FileCheck %s --check-prefix=OBJECT
// RUN: obelisk --help | FileCheck %s --check-prefix=HELP
// RUN: not obelisk --target=wasm32 -emit-llvm %s -o %t.bad.ll 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-TARGET
//
// Exercise executable-link validation without depending on a host Emscripten
// installation. Relocating the driver lets this test provide a private target
// support tree, while deliberately incomplete sysroots reach each wasm-only
// validation boundary before lld is invoked.
// RUN: %split-file %s %t
// RUN: cp %obelisk %t/no-runtime/bin/obelisk
// RUN: cp %obelisk %t/no-libs/bin/obelisk
// RUN: not %t/no-runtime/bin/obelisk --target=wasm64 -O0 \
// RUN:   --sysroot=%t/missing %s -o %t/missing.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING-SYSROOT
// RUN: not %t/no-runtime/bin/obelisk --target=wasm64 -O0 \
// RUN:   --sysroot=%t/wasm32-sysroot %s -o %t/wasm32.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WASM32-SYSROOT
// RUN: not %t/no-runtime/bin/obelisk --target=wasm64 -O0 \
// RUN:   --sysroot=%t/no-runtime-sysroot %s -o %t/no-runtime.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING-RUNTIME
// RUN: not %t/no-libs/bin/obelisk --target=wasm64 -O0 \
// RUN:   --sysroot=%t/no-libs-sysroot %s -o %t/no-libs.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING-LIBS

module wasm_target;
  logic [63:0] value;

  initial begin
    value = 64'h1234_5678_9abc_def0;
    #1 value++;
    $display("value=%h", value);
  end
endmodule

// LLVM: target datalayout = "e-m:e-p:64:64-{{.*}}"
// LLVM-NEXT: target triple = "wasm64-unknown-emscripten"
// LLVM: define i32 @main(
// LLVM: call i32 @obelisk_rt_v1_scheduler_run{{.*}}(

// OBJECT: Format: WASM
// OBJECT-NEXT: Arch: wasm64
// OBJECT-NEXT: AddressSize: 64bit
// OBJECT: Type: CODE
// OBJECT: Type: DATA
// OBJECT: Name: target_features

// HELP-DAG: -c{{ *}}Emit a relocatable object for the selected target
// HELP-DAG: -emit-llvm{{ *}}Emit textual LLVM IR for the selected target
// HELP-DAG: --sysroot=<dir>{{ *}}Use <dir> for target C and C++ link inputs
// HELP-DAG: --target=<native|wasm64>
// HELP-DAG: Select the code-generation target

// BAD-TARGET: obelisk: error: unsupported target 'wasm32'; expected native or wasm64
// MISSING-SYSROOT: obelisk: error: wasm sysroot '{{.*}}/missing' is not a directory
// WASM32-SYSROOT: has no wasm64-emscripten libraries; a wasm32 sysroot cannot satisfy the 64-bit runtime ABI
// MISSING-RUNTIME: obelisk: error: wasm support is missing '{{.*}}/libobelisk_rt.a'
// MISSING-LIBS: obelisk: error: wasm sysroot is missing '{{.*}}/libstubs.a'

//--- no-runtime/bin/placeholder
// Split-file fixture: the driver executable is copied beside this file.

//--- no-runtime/lib/obelisk/targets/wasm64-unknown-emscripten/.complete
// Split-file fixture: target support stamp without a runtime archive.

//--- wasm32-sysroot/placeholder
// Split-file fixture: existing sysroot without wasm64 libraries.

//--- no-runtime-sysroot/lib/wasm64-emscripten/placeholder
// Split-file fixture: wasm64 library directory without target runtime support.

//--- no-libs/bin/placeholder
// Split-file fixture: the driver executable is copied beside this file.

//--- no-libs/lib/obelisk/targets/wasm64-unknown-emscripten/.complete
// Split-file fixture: complete target support stamp.

//--- no-libs/lib/obelisk/targets/wasm64-unknown-emscripten/libobelisk_rt.a
// Split-file fixture: runtime presence is enough to reach sysroot validation.

//--- no-libs-sysroot/lib/wasm64-emscripten/placeholder
// Split-file fixture: wasm64 library directory without support archives.
