// RUN: obelisk -O3 --native-scheduler=eval -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM --implicit-check-not=obelisk_rt_v1_native_state_bind_shared < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: %t.eval | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// IEEE 1800-2023 35.7: an unused DPI export must not change SV execution.
// The native scheduler must retain canonical state publication for exports,
// even when the clocked work would otherwise qualify for shared state.
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
// LLVM-DAG: call i32 @obelisk_rt_v1_native_state_sync
// CHECK: DPI EXPORT STARTUP PASS ticks=4
