// RUN: %target_clang %target_pic -c %S/Inputs/native_aot_dpi_host.c -o %t.o
// RUN: obelisk -O3 --vpi=off --mlir-timing %s %t.o -o %t.aot 2> %t.timing
// RUN: FileCheck %s --check-prefix=PLAN < %t.timing
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.aot 2>&1 | FileCheck %s

// IEEE 1800-2023 35.5.3: a context import may call any export. This export
// reads no design state and VPI is off, so the process calling the imports
// stays in the native AOT plan, and native lowering, after rewrites that
// drop compute-graph effect summaries, admits the same actors as planning.

// PLAN-NOT: withheld actor
// PLAN: obelisk native eligibility: eligible=1 fully_eligible=1
// PLAN-NOT: withheld actor
// CHECK: count=9
// CHECK: obelisk-signal-diagnostics
// CHECK-SAME: aot_node_executions={{[1-9][0-9]*}}
// CHECK-SAME: aot_fallbacks=0

module native_aot_dpi_context_import;
  import "DPI-C" context function void start();
  import "DPI-C" context function int scan();
  export "DPI-C" function get_period;
  function int get_period();
    return 1000;
  endfunction
  bit clk;
  int count;
  always #5 clk = ~clk;
  always @(negedge clk) begin
    start();
    count <= count + scan();
  end
  initial begin
    #100 $display("count=%0d", count);
    $finish;
  end
endmodule
