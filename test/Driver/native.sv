// RUN: env PATH=/nonexistent %obelisk %s -o %t.exe
// RUN: obelisk --print-host-c-runtime > %t.host-c-runtime
// RUN: %python %S/../Inputs/check-host-c-runtime.py %t.host-c-runtime
// RUN: %host-c-runtime-test 2>&1 \
// RUN:   | FileCheck %s --check-prefix=HOST-DISCOVERY-ERROR
// RUN: llvm-readelf -h -l -d %t.exe | FileCheck %s --check-prefix=ELF \
// RUN:   --implicit-check-not='(RPATH)' --implicit-check-not='(RUNPATH)'
// RUN: llvm-strings %t.exe | FileCheck %s --check-prefix=PATHS \
// RUN:   --implicit-check-not='/glibc/' \
// RUN:   --implicit-check-not='workspace/obelisk' \
// RUN:   --implicit-check-not='/home/runner/'
// RUN: %t.exe > %t.exe.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.exe.out
// RUN: obelisk -O0 --compile-threads=1 %s -o %t.o0.exe
// RUN: llvm-readelf --symbols %t.o0.exe \
// RUN:   | FileCheck %s --check-prefix=GC-SECTIONS \
// RUN:     --implicit-check-not=obelisk_rt_v1_queue_create \
// RUN:     --implicit-check-not=obelisk_rt_v1_vpi_startup
// RUN: %t.o0.exe > %t.o0.out
// RUN: diff -u %t.exe.out %t.o0.out
// RUN: obelisk -O1 --compile-threads=1 %s -o %t.o1.t1.exe
// RUN: %t.o1.t1.exe > %t.o1.t1.out
// RUN: diff -u %t.exe.out %t.o1.t1.out
// RUN: obelisk -O1 --compile-threads=4 %s -o %t.o1.t4.exe
// RUN: %t.o1.t4.exe > %t.o1.t4.out
// RUN: diff -u %t.o1.t1.out %t.o1.t4.out
// RUN: obelisk -O3 --compile-threads=4 %s -o %t.o3.t4.exe
// RUN: %t.o3.t4.exe > %t.o3.t4.out
// RUN: diff -u %t.exe.out %t.o3.t4.out
// RUN: obelisk -c %s -o %t.o
// RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=OBJECT
// RUN: obelisk -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: not obelisk --threads=2 %s -o %t.threads 2>&1 \
// RUN:   | FileCheck %s --check-prefix=THREADS
// RUN: obelisk --vpi=read %s -o %t.vpi
// RUN: %t.vpi > %t.vpi.out
// RUN: diff -u %t.exe.out %t.vpi.out
// RUN: not obelisk --sysroot=%t.missing %s -o %t.missing.exe 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SYSROOT

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

// PATHS: obelisk_rt_v1_scheduler_run

// The non-LTO runtime uses one ELF section per function so the linker can
// retain scheduler support without pulling unrelated container or VPI APIs.
// GC-SECTIONS: obelisk_rt_v1_scheduler_run_aot

// LLVM: target triple = "{{.*}}-unknown-linux-gnu"
// LLVM-DAG: @unit_0.__obelisk_schedule_ranks = internal constant [1 x i32] [i32 2]
// LLVM-DAG: @__obelisk_aot_schedule_plan_v1
// LLVM-DAG: call i64 @obelisk_rt_v1_process_spawn
// LLVM: define i32 @main(i32
// LLVM-SAME: ptr
// LLVM: call i32 @obelisk_rt_v1_scheduler_install_aot
// LLVM: call i32 @obelisk_rt_v1_scheduler_run_aot

// THREADS: native executable generation currently requires --threads=1
// SYSROOT: --sysroot is only valid with --target=wasm32

// HOST-DISCOVERY-ERROR: host link job has no usable dynamic linker
// HOST-DISCOVERY-ERROR: host C runtime is missing Scrt1.o
// HOST-DISCOVERY-ERROR: clang searched:
