// RUN: talos -O3 --native-scheduler=eval --top=clockless_top %s -o %t.clockless
// RUN: env TALOS_RT_SIGNAL_DIAGNOSTICS=1 %t.clockless 2> %t.clockless.trace | FileCheck %s
// RUN: FileCheck %s --check-prefix=EVAL < %t.clockless.trace
// RUN: talos -O3 --native-scheduler=eval --top=periodic_top %s -o %t.periodic
// RUN: env TALOS_RT_SIGNAL_DIAGNOSTICS=1 %t.periodic 2> %t.periodic.trace | FileCheck %s
// RUN: FileCheck %s --check-prefix=PERIODIC < %t.periodic.trace

// IEEE 1800-2023 6.8, 10.4.2: retaining an eval route across a runtime
// checkpoint must neither hide an NBA's X/Z update nor prevent recovery to
// known values. Unequal clock phases keep the clockless wrapper; equal phases
// normalize to a periodic plan. Both match the generic scheduler's output.
// That neither wrapper reseeds variant selectors or pending proofs is checked
// on the generated IR in
// Conversion/simulation-eval-clockless-route-retention.mlir and
// Conversion/simulation-periodic-reset-publication.mlir.
module checkpoint_route_retention #(parameter int LOW = 4, parameter int HIGH = 6);
  logic clk = 0;
  integer cycles = 0;
  initial forever begin
    #LOW clk <= 0;
    #HIGH clk <= 1;
    cycles += 1;
  end
  logic [7:0] q[4];
  logic [7:0] inverted[4];
  for (genvar i = 0; i < 4; i++) begin : g
    initial q[i] = i;
    always @(posedge clk) begin
      if (cycles == 4) q[i] <= 'x;
      else if (cycles == 6) q[i] <= 'z;
      else if (cycles == 8) q[i] <= i;
      else q[i] <= q[i] + 1;
    end
    always_comb inverted[i] = ~q[i];
  end
  logic [255:0] line;
  real fraction = 1.25;
  string message;
  function automatic logic [255:0] make_line(input logic [7:0] data);
    logic [255:0] result;
    for (int i = 0; i < 32; i++) result[i*8 +: 8] = data;
    return result;
  endfunction
  always_comb line = make_line(inverted[3]);
  initial begin
    message = "runtime checkpoint";
    $display("%f %s", fraction, message);
    repeat (10) begin
      @(posedge clk);
      #1;
      $display("%0d %h %h %h", cycles, q[3], inverted[3], line[7:0]);
    end
    $finish;
  end
endmodule

module clockless_top;
  checkpoint_route_retention #(.LOW(4), .HIGH(6)) dut();
endmodule

module periodic_top;
  checkpoint_route_retention #(.LOW(5), .HIGH(5)) dut();
endmodule

// EVAL: eval_dispatches={{[1-9][0-9]*}}
// PERIODIC: periodic_preparations={{[1-9][0-9]*}}
// CHECK: 1.250000 runtime checkpoint
// CHECK-NEXT: 1 04 fb fb
// CHECK-NEXT: 2 05 fa fa
// CHECK-NEXT: 3 06 f9 f9
// CHECK-NEXT: 4 xx xx xx
// CHECK-NEXT: 5 xx xx xx
// CHECK-NEXT: 6 zz xx xx
// CHECK-NEXT: 7 xx xx xx
// CHECK-NEXT: 8 03 fc fc
// CHECK-NEXT: 9 04 fb fb
// CHECK-NEXT: 10 05 fa fa
