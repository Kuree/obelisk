// RUN: talos -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -
// RUN: talos -emit-llvm %s -o %t.hybrid.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t.hybrid.ll
// RUN: not talos --native-scheduler=aot %s -o %t.aot 2>&1 \
// RUN:   | FileCheck %s --check-prefix=AOT-ERROR

`timescale 1ns/1ps
module native_dynamic_delay;
  byte unsigned delay8;
  int unsigned delay32;
  longint unsigned delay64;

  initial begin
    delay8 = 1;
    delay32 = 2;
    delay64 = 3;
    #(delay8) $display("delay8");
    #(delay32) $display("delay32");
    #(delay64) $display("delay64");
  end
endmodule

// CHECK: delay8
// CHECK-NEXT: delay32
// CHECK-NEXT: delay64
// AOT-ERROR: design is ineligible for native AOT scheduling: dynamic deadline
// HYBRID-NOT: @unit_0.__talos_bytecode_continuations
// HYBRID-DAG: call i64 @talos_rt_v1_process_spawn
// HYBRID-DAG: call i32 @talos_rt_v1_scheduler_run
