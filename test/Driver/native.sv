// RUN: env -i PATH=/nonexistent %obelisk %s -o %t.exe
// RUN: llvm-readelf -h -l -d %t.exe | FileCheck %s --check-prefix=ELF \
// RUN:   --implicit-check-not='Shared library:' \
// RUN:   --implicit-check-not='(RPATH)' --implicit-check-not='(RUNPATH)'
// RUN: llvm-readelf --version-info %t.exe \
// RUN:   | FileCheck %s --check-prefix=VERSIONS \
// RUN:     --implicit-check-not='GLIBC_{{2\.(29|[3-9][0-9]|[1-9][0-9]{2,})|([3-9]|[1-9][0-9]+)\.}}'
// RUN: llvm-strings %t.exe | FileCheck %s --check-prefix=PATHS \
// RUN:   --implicit-check-not='target-sysroots' \
// RUN:   --implicit-check-not='workspace/obelisk' \
// RUN:   --implicit-check-not='/home/runner/'
// RUN: %t.exe > %t.exe.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.exe.out
// RUN: obelisk -O0 --compile-threads=1 %s -o %t.o0.exe
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
// RUN: %native_support/glibc/lib/x86_64-linux-gnu/ld-2.28.so \
// RUN:   --library-path %native_support/glibc/lib/x86_64-linux-gnu \
// RUN:   %t.exe | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk --sysroot=%native_support/glibc %s -o %t.sysroot.exe
// RUN: %t.sysroot.exe | FileCheck %s --check-prefix=OUTPUT
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
// OBJECT: Machine: Advanced Micro Devices X86-64

// ELF: Type: DYN (
// ELF: Requesting program interpreter: /lib64/ld-linux-x86-64.so.2
// ELF: Shared library: [libc.so.6]
// ELF: Shared library: [ld-linux-x86-64.so.2]
// ELF: Shared library: [libm.so.6]
// ELF: Shared library: [libpthread.so.0]
// ELF: Shared library: [libdl.so.2]
// ELF: Shared library: [librt.so.1]

// VERSIONS: Name: GLIBC_2.

// PATHS: obelisk_rt_v1_scheduler_run

// LLVM: target triple = "x86_64-unknown-linux-gnu"
// LLVM: @unit_0.__obelisk_schedule_ranks = internal constant [1 x i32] [i32 2]
// LLVM: call i32 @obelisk_rt_v1_scheduler_add_planned
// LLVM: define i32 @main(i32
// LLVM-SAME: ptr
// LLVM: call i32 @obelisk_rt_v1_scheduler_run

// THREADS: native executable generation currently requires --threads=1
// SYSROOT: target sysroot input
// SYSROOT-SAME: is missing or escapes
