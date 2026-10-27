// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=auto sim-opt-level=3 stop-after=lowering' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=KERNELS < %t.mlir

// IEEE 1800-2023 4.5 and 23.3.3: the always_ff processes of every instance
// clocked through port connections of one clock run as one clocked kernel,
// whose NBAs to roots nothing can observe early become direct stores. The
// testbench calls a task with an output formal, which has storage of its own
// that the caller copies to the actual on return (13.5.1). The task's writes
// to that storage reach no design root, so they must not hide the clock's
// port copies or make every root look written in every phase.

module clkgen(output logic clk);
  initial begin
    clk <= 1'b0;
    forever begin
      #5 clk <= 1'b0;
      #5 clk <= 1'b1;
    end
  end
endmodule

module stage(input logic clk, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk) q <= d + 8'd1;
endmodule

module core(input logic clk_p, output logic [7:0] out);
  logic [7:0] a, b;
  stage s0(.clk(clk_p), .d(b), .q(a));
  stage s1(.clk(clk_p), .d(a), .q(b));
  assign out = b;
endmodule

module top;
  logic clk;
  logic [7:0] out;
  logic [7:0] values[2];
  logic [7:0] count;
  clkgen gen(.clk(clk));
  core c(.clk_p(clk), .out(out));
  always_ff @(posedge clk) count <= out;
  task read_values(input int n, output logic [7:0] data[2]);
    data[0] = c.a;
    data[1] = c.b + n;
  endtask
  initial begin
    repeat (4) begin
      @(posedge clk);
      #1;
      read_values(1, values);
    end
    $display("%0d %0d %0d", values[0], values[1], count);
    $finish;
  end
endmodule

// The stages and `count`, clocked through port copies of one clock, run in one
// kernel that stores their registers directly.
// CHECK: simulation.spawn @[[KERNEL:__talos_clocked_[0-9_]+]](
// CHECK: simulation.task.call
// CHECK: simulation.func private @[[KERNEL]](
// CHECK: simulation.suspend.edge posedge
// CHECK-NOT: simulation.nba.enqueue
// CHECK: simulation.ref.store
// CHECK-NOT: simulation.nba.enqueue
// CHECK: simulation.ref.store
// CHECK-NOT: simulation.nba.enqueue
// CHECK: simulation.ref.store
// CHECK-NOT: simulation.nba.enqueue
// CHECK: }

// KERNELS-COUNT-1: simulation.spawn @__talos_clocked_
// KERNELS-NOT: simulation.spawn @__talos_clocked_
