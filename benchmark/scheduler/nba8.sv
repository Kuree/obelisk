`timescale 1ns/1ns

`ifndef CYCLES
`define CYCLES 250
`endif

`ifndef DORMANT_WAITERS
`define DORMANT_WAITERS 0
`endif

module nba8;
  timeunit 1ns;
  timeprecision 1ns;

  parameter int CYCLES = `CYCLES;
  parameter int DORMANT_WAITERS = `DORMANT_WAITERS;
  bit clock = 0;
  bit reset = 1;
  bit [31:0] tick = 0;
  bit [31:0] a [0:7];
  bit [31:0] b [0:7];
  bit [31:0] c [0:7];
  bit [31:0] d [0:7];
  bit [31:0] updates [0:7];
  bit [4095:0] dormant_signals;

  function automatic bit [31:0] mix32(input bit [31:0] value);
    bit [31:0] mixed;
    begin
      mixed = value ^ (value << 13);
      mixed = mixed ^ (mixed >> 17);
      mixed = mixed ^ (mixed << 5);
      mix32 = mixed;
    end
  endfunction

  for (genvar lane = 0; lane < 8; lane++) begin : lanes
    always @(posedge clock) begin
      bit [31:0] next_a;
      if (reset) begin
        a[lane] <= 32'h1020_3040 ^ lane;
        b[lane] <= 32'h5060_7080 ^ (lane * 32'h0101_0101);
        c[lane] <= 32'h90a0_b0c0 ^ (lane * 32'h0011_0011);
        d[lane] <= 32'hd0e0_f001 ^ (lane * 32'h0001_0001);
        updates[lane] <= 0;
      end else begin
        next_a = mix32(d[lane] ^ tick ^ (lane * 32'h9e37_79b9));
        a[lane] <= next_a;
        if (next_a[0])
          a[lane] <= next_a ^ 32'ha5a5_0000 ^ lane;
        b[lane] <= mix32(a[lane] + 32'h1111_0001 + lane);
        c[lane] <= mix32(b[lane] ^ 32'h2222_0002);
        d[lane] <= mix32(c[lane] + 32'h3333_0003 + lane);
        updates[lane] <= updates[lane] + 1;
        if (tick == CYCLES - 1)
          $display("LANE %0d %08h %08h %08h %08h %08h",
                   lane, a[lane], b[lane], c[lane], d[lane], updates[lane]);
      end
    end
  end

  for (genvar waiter = 0; waiter < DORMANT_WAITERS; waiter++) begin : dormant
    always @(posedge dormant_signals[1024 + waiter]) begin
    end
  end

  initial begin
    #1 clock = 1;
    #1 clock = 0;
    reset = 0;
    for (int cycle = 0; cycle < CYCLES; cycle++) begin
      tick = cycle;
      #1 clock = 1;
      #1 clock = 0;
    end
    #1;
    $finish;
  end
endmodule
