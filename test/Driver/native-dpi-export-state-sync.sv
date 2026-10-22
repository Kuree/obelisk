// RUN: obelisk -O3 --native-scheduler=eval -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: %t.eval | FileCheck %s
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// IEEE 1800-2023 35.7: an unused DPI export must not change SV execution.
// An export runs in its importing caller's call chain (35.5.3) and must see
// that caller's stores, so generated code and the runtime share one state
// image here as in a design without exports.
module native_dpi_export_state_sync;
  logic clk = 0;
  int unsigned ticks = 0;
  always #5 clk = ~clk;
  always @(posedge clk) ticks <= ticks + 1;

  function automatic int read_ticks();
    return ticks;
  endfunction
  export "DPI-C" function read_ticks;

  initial begin
    #36;
    if (ticks != 4) $fatal(1, "bad tick count: %0d", ticks);
    $display("DPI EXPORT STARTUP PASS ticks=%0d", ticks);
    $finish;
  end
endmodule

// LLVM-DAG: define {{.*}}i32 @read_ticks()
// LLVM-DAG: call i32 @obelisk_rt_v1_native_state_bind_shared
// LLVM-DAG: call i32 @obelisk_rt_v1_native_state_sync
// CHECK: DPI EXPORT STARTUP PASS ticks=4
