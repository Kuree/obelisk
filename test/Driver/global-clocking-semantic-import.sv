// RUN: obelisk --std=1800-2017 -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk --std=1800-2017 -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK

module leaf;
  logic active;
  initial @($global_clock);
  assert property (@$global_clock
      $past_gclk(active) || $rose_gclk(active) || $fell_gclk(active) ||
      $stable_gclk(active) || $changed_gclk(active) ||
      $future_gclk(active) || $rising_gclk(active) ||
      $falling_gclk(active) || $steady_gclk(active) ||
      $changing_gclk(active));
endmodule

module subsystem;
  logic clk;
  global clocking gcb @(posedge clk); endclocking
  leaf child();
endmodule

module top;
  subsystem left();
  subsystem right();
endmodule

// Each elaborated use freezes the effective global clock selected by Slang's
// hierarchical lookup. This is deliberately a source-semantic test; lowering
// and execution are covered with hand-authored semantic MLIR.
// SLANG-DAG: slang.symbol.clocking_block @{{[^ ]+}} attributes {{.*}}hierarchical_name = "top.left.gcb", is_default = false, is_global = true
// SLANG-DAG: slang.symbol.clocking_block @{{[^ ]+}} attributes {{.*}}hierarchical_name = "top.right.gcb", is_default = false, is_global = true
// SLANG-DAG: slang.expression.call attributes {{.*}}callee_name = "$global_clock"{{.*}}clocking_block_event{{.*}}clocking_event_edge = 1 : i32{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: slang.expression.call attributes {{.*}}callee_name = "$global_clock"{{.*}}clocking_block_event{{.*}}clocking_event_edge = 1 : i32{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$past_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$past_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$rose_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$rose_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$fell_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$fell_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$stable_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$stable_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$changed_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$changed_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$future_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$future_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$rising_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$rising_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$falling_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$falling_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$steady_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$steady_gclk"{{.*}}clocking_event_path = "top.right.clk"
// SLANG-DAG: callee_name = "$changing_gclk"{{.*}}clocking_event_path = "top.left.clk"
// SLANG-DAG: callee_name = "$changing_gclk"{{.*}}clocking_event_path = "top.right.clk"

// OBELISK-DAG: obelisk.sv.expression.call attributes {{.*}}callee_name = "$global_clock"{{.*}}clocking_block_event{{.*}}clocking_event_edge = 1 : i32{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: obelisk.sv.expression.call attributes {{.*}}callee_name = "$global_clock"{{.*}}clocking_block_event{{.*}}clocking_event_edge = 1 : i32{{.*}}clocking_event_path = "top.right.clk"
// `$past_gclk` and `$future_gclk` return the expression value, including its
// four-state plane; the other eight functions return a one-bit predicate.
// OBELISK-DAG: callee_name = "$past_gclk"{{.*}}clocking_event_path = "top.left.clk"{{.*}}semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
// OBELISK-DAG: callee_name = "$past_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$rose_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$rose_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$fell_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$fell_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$stable_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$stable_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$changed_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$changed_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$future_gclk"{{.*}}clocking_event_path = "top.left.clk"{{.*}}semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
// OBELISK-DAG: callee_name = "$future_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$rising_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$rising_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$falling_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$falling_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$steady_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$steady_gclk"{{.*}}clocking_event_path = "top.right.clk"
// OBELISK-DAG: callee_name = "$changing_gclk"{{.*}}clocking_event_path = "top.left.clk"
// OBELISK-DAG: callee_name = "$changing_gclk"{{.*}}clocking_event_path = "top.right.clk"
