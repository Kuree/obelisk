// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: not obelisk -O3 --native-scheduler=aot %s -o %t.aot 2>&1 | FileCheck %s --check-prefix=AOT-ERROR

`timescale 1ns / 1ns

module procedural_edge_cell(
    input wire clock, reset, data, input wire [1:0] sliced_data,
    output logic destination, output logic [3:0] sliced);
  always @(posedge clock) begin
    destination <= data;
    sliced[2:1] = sliced_data;
  end
  // A distinct writer must share the destination path key. An unqualified
  // write overrides even a same-target value that is still pending.
  always @(posedge reset) begin
    destination = data;
    if (!data)
      sliced = 0;
    else
      // This disjoint write must not cancel the pending path bits [2:1].
      sliced[0] = 1;
  end
  specify
    (posedge clock => (destination +: data)) = 4;
    (posedge clock *> (sliced +: sliced_data)) = 3;
  endspecify
endmodule

module specify_edge_procedural_output;
  logic clock, reset, data;
  logic [1:0] sliced_data;
  logic destination;
  logic [3:0] sliced;
  procedural_edge_cell dut(clock, reset, data, sliced_data, destination,
                           sliced);

  initial begin
    clock = 0;
    reset = 0;
    data = 0;
    sliced_data = 0;
    #1 reset = 1;
    #1 reset = 0;
    #1 data = 1;
    sliced_data = 2'b11;
    #1 clock = 1;
    #1 clock = 0;
    $display("pending %0t %b %b", $time, destination, sliced);
    // Same target as the pending qualified write: this reset activation is
    // unqualified and therefore publishes now and cancels the old deadline.
    #1 reset = 1;
    #1 reset = 0;
    $display("override %0t %b %b", $time, destination, sliced);
    #5;
    $display("stable %0t %b %b", $time, destination, sliced);
    data = 0;
    #1 clock = 1;
    #1 clock = 0;
    $display("repeat-pending %0t %b", $time, destination);
    #4 $display("repeat-done %0t %b", $time, destination);
    // The clock writer executes in Active and stages an NBA path write. The
    // following #0 reset wake is a later same-time Active iteration and must
    // override/cancel it through the shared destination key.
    data = 1;
    #1 clock = 1;
    #0 reset = 1;
    #0 $display("same-time-override %0t %b", $time, destination);
    #1 reset = 0;
    data = 0;
    #1 reset = 1;
    #1 reset = 0;
    #2 $display("same-time-stable %0t %b", $time, destination);
    $finish;
  end
endmodule

// SIM-DAG: obelisk_sim.ref.store_inertial_path{{.*}}nonblocking = true
// SIM-DAG: obelisk_sim.ref.store_inertial_path{{.*}}nonblocking = false
// SIM-DAG: procedural_wake_kind = 1 : i32
// SIM-DAG: procedural_wake_kind = 3 : i32
// CHECK: pending 5 0 0000
// CHECK-NEXT: override 7 1 0111
// CHECK-NEXT: stable 12 1 0111
// CHECK-NEXT: repeat-pending 14 1
// CHECK-NEXT: repeat-done 18 0
// CHECK-NEXT: same-time-override 19 1
// CHECK-NEXT: same-time-stable 24 0
// AOT-ERROR: design is ineligible for native AOT scheduling: procedural path scheduling is runtime-owned
