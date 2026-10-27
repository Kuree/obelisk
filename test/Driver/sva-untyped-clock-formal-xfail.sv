// XFAIL: *
// RUN: talos --std=1800-2023 -emit-talos %s -o /dev/null

// IEEE clock-flow permits an untyped assertion formal to carry a clocking
// event. The current Slang semantic AST replaces this legal expansion with an
// InvalidAssertionExpr, so retain the source case without patching Slang.
module sva_untyped_clock_formal_xfail;
  logic clk0, clk1, value;

  sequence sampled(clock_formal);
    @(clock_formal) value;
  endsequence

  assert property (sampled(posedge clk0 or negedge clk1));
endmodule
