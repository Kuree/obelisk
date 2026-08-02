`timescale 1ns/1ns

`ifndef CYCLES
`define CYCLES 100000
`endif

// FPGA/CGRA-shaped control shell around a gated combinational convergence
// component. The two monotone equations form a real SCC while enabled; when
// disabled the component settles to zero and its direct Tier-2 ingress stays
// cold. The clocked accumulator and controller remain Tier 1.
module gated_scc;
  timeunit 1ns;
  timeprecision 1ns;

  parameter int CYCLES = `CYCLES;
  bit clock = 0;
  bit reset = 1;
  bit enable = 0;
  bit [31:0] mask = 32'hffff_ffff;
  bit [31:0] left;
  bit [31:0] right;
  bit [31:0] accumulator = 0;
  bit [31:0] activations = 0;

  always_comb begin
    if (enable)
      left = right & mask;
    else
      left = 0;
  end

  always_comb begin
    if (enable)
      right = left & mask;
    else
      right = 0;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      accumulator <= 32'h1357_9bdf;
      activations <= 0;
    end else begin
      accumulator <= {accumulator[30:0], accumulator[31]} ^
                     left ^ right ^ 32'h9e37_79b9;
      if (enable)
        activations <= activations + 1;
    end
  end

  initial begin
    #1 clock = 1;
    #1 clock = 0;
    reset = 0;
    for (int cycle = 0; cycle < CYCLES; cycle++) begin
      enable = cycle[4] & cycle[1];
      mask = 32'hffff_ffff >> cycle[2:0];
      #1 clock = 1;
      #1 clock = 0;
    end
    #1;
    $display("GATED_SCC %08h %08h %08h %08h",
             accumulator, activations, left, right);
    $finish;
  end
endmodule
