// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

module always_comb_edge_cell(input wire data, output logic destination);
  always_comb destination = data;
  specify
    (posedge data => (destination +: data)) = 2;
  endspecify
endmodule

module specify_edge_always_comb_output;
  logic data = 1;
  logic destination;
  always_comb_edge_cell dut(data, destination);
  initial begin
    #1 $display("time-zero-pending %0t %b", $time, destination);
    #2 $display("time-zero-done %0t %b", $time, destination);
    data = 0;
    #1 $display("fall-immediate %0t %b", $time, destination);
    data = 1;
    #1 $display("repeat-pending %0t %b", $time, destination);
    #2 $display("repeat-done %0t %b", $time, destination);
    $finish;
  end
endmodule

// The qualification logic is in the implicit actor loop header, once per
// activation, rather than duplicated at the assignment leaf.
// SIM: procedural_wake_kind = 2 : i32
// SIM-COUNT-1: obelisk_sim.ref.store_inertial_path
// CHECK: time-zero-pending 1 x
// CHECK-NEXT: time-zero-done 3 1
// CHECK-NEXT: fall-immediate 4 0
// CHECK-NEXT: repeat-pending 5 0
// CHECK-NEXT: repeat-done 7 1
