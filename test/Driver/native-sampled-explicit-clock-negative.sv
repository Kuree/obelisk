// RUN: not obelisk --std=1800-2023 -emit-llvm -DMISMATCH %s 2>&1 | FileCheck %s --check-prefix=MISMATCH
// RUN: not obelisk --std=1800-2023 -emit-llvm -DIFF_CLOCK %s 2>&1 | FileCheck %s --check-prefix=IFF
// RUN: not obelisk --std=1800-2023 -emit-llvm -DCOMPUTED %s 2>&1 | FileCheck %s --check-prefix=COMPUTED
// RUN: not obelisk --std=1800-2023 -emit-llvm %s 2>&1 | FileCheck %s --check-prefix=UNBOUND

module native_sampled_explicit_clock_negative;
  logic clk = 0;
  logic other = 0;
  logic enable = 1;
  logic data = 0;

`ifdef MISMATCH
  always @(posedge clk)
    $display("%b", $past(data, , , @(posedge other)));
`elsif IFF_CLOCK
  always @(posedge clk)
    $display("%b", $rose(data, @(posedge clk iff (enable && data))));
`elsif COMPUTED
  bad_operand: assert property (@(posedge clk)
      $past(data & enable, , , @(posedge other)));
`else
  initial begin
    #1 $display("%b", $changed(data, @(posedge clk)));
  end
`endif
endmodule

// MISMATCH: error: $past genuinely alternate clocks are currently executable only in a statically clocked concurrent predicate
// IFF: error: $rose explicit clocks currently require one direct named-signal edge and an optional direct named iff condition
// COMPUTED: error: alternate-clock sampled values currently require direct named packed source, gate, clock-iff condition, and clock signals
// UNBOUND: error: $changed explicit clock requires a matching statically enclosing direct event control
