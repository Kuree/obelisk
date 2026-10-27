// REQUIRES: native-elf
// RUN: env PATH=/nonexistent %talos %s -o %t.exe
// RUN: llvm-readelf -h -l -d %t.exe | FileCheck %s --check-prefix=ELF --implicit-check-not='(RPATH)' --implicit-check-not='(RUNPATH)'
// RUN: llvm-strings %t.exe | FileCheck %s --check-prefix=PATHS --implicit-check-not='/glibc/' --implicit-check-not='workspace/talos' --implicit-check-not='/home/runner/'
// RUN: %t.exe > %t.exe.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.exe.out
// RUN: talos -c %s -o %t.o
// RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=OBJECT
// RUN: talos -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

module native_driver;
  logic [7:0] value;

  initial begin
    value = 8'd40;
    $display("first=%0d", value);
    #1;
    value = value + 2;
    $display("second=%0d", value);
  end

  final $display("final=%0d", value);
endmodule

// OUTPUT: first=40
// OUTPUT-NEXT: second=42
// OUTPUT-NEXT: final=42

// OBJECT: Type: REL (Relocatable file)

// ELF: Type: DYN (
// ELF: Requesting program interpreter: /{{.*}}
// ELF: Shared library: [libc.so.6]
// ELF: Shared library: [libm.so.6]
// ELF: FLAGS_1
// ELF-SAME: PIE

// PATHS: talos_rt_v1_scheduler_run

// The non-LTO runtime uses one ELF section per function so the linker can
// retain scheduler support without pulling unrelated container or VPI APIs.
// GC-SECTIONS: talos_rt_v1_scheduler_run_aot

// LLVM: target triple = "{{.*}}-unknown-linux-gnu"
// LLVM-DAG: @unit_0.__talos_schedule_ranks = internal constant [1 x i32] [i32 2]
// LLVM-DAG: @__talos_aot_schedule_plan_v1
// LLVM-DAG: call i64 @talos_rt_v1_process_spawn
// LLVM: define i32 @main(i32
// LLVM-SAME: ptr
// LLVM: call i32 @talos_rt_v1_scheduler_install_aot
// LLVM: call i32 @talos_rt_v1_scheduler_run_aot

// THREADS: native executable generation currently requires --threads=1
// SYSROOT: --sysroot is only valid with --target=wasm32

// HOST-DISCOVERY-ERROR: host link job has no usable dynamic linker
// HOST-DISCOVERY-ERROR: host C runtime is missing Scrt1.o
// HOST-DISCOVERY-ERROR: clang searched:

// The discovered host C-runtime inputs are absolute paths that exist; discovery
// rejects anything else before printing.
// HOST-RUNTIME: {{^}}dynamic-linker=/{{.+}}
// HOST-RUNTIME-NEXT: {{^}}crt1=/{{.+}}
// HOST-RUNTIME-NEXT: {{^}}crti=/{{.+}}
// HOST-RUNTIME-NEXT: {{^}}crtn=/{{.+}}
// HOST-RUNTIME-NEXT: {{^}}libc=/{{.+}}
// HOST-RUNTIME-NEXT: {{^}}libm=/{{.+}}
// HOST-RUNTIME-NOT: {{.}}
