// RUN: %split-file %s %t
// RUN: %target_clang -fPIC -shared -nostdlib %t/plugin.c -I%resource_dir/include -o %t/plugin.so
// RUN: obelisk -O3 --vpi=full --native-scheduler=eval %t/design.sv %t/plugin.so -o %t/eval
// RUN: obelisk -O3 --vpi=full --native-scheduler=generic %t/design.sv %t/plugin.so -o %t/generic
// RUN: %t/eval > %t/eval.out
// RUN: %t/generic > %t/generic.out
// RUN: diff -u %t/generic.out %t/eval.out
// RUN: FileCheck %s < %t/eval.out
// RUN: obelisk -O3 --vpi=full --native-scheduler=eval -emit-llvm %t/design.sv -o %t.ll
// RUN: FileCheck %s --check-prefix=QUEUE < %t.ll

// Force an entire wide register across clocked overlapping NBA updates.
// Release and X/Z deposits must retain
// canonical visibility and later procedural writes must recover the value.
// IEEE 1800-2023 4.4-4.6, 10.4.2, 10.6.2, 38.34.
//--- design.sv
module shared_ordered_nba_vpi;
  logic clk = 0;
  logic [127:0] wide = 0;
  // LRM 4.6(b), 10.4.2: include cold initialization in the same ordered root
  // that later executes generated writes and VPI force/release fallback.
  initial begin
    wide <= 128'hffffffffffffffffffffffffffffffff;
    wide <= 128'd7;
  end
  import "DPI-C" function int mutate(input int action);
  always #5 clk = ~clk;
  always @(posedge clk) begin
    wide[63:0] <= 64'h1;
    wide[63:0] <= 64'h2;
    wide[127:64] <= wide[127:64] + 1;
  end
  initial begin
    automatic string message = "vpi";
    #1 $display("initialized=%h", wide);
    #5;
    if (mutate(1)) $fatal(1, "force failed");
    #10 $display("%s forced=%h", message, wide);
    if (mutate(2)) $fatal(1, "release failed");
    if (mutate(3)) $fatal(1, "deposit failed");
    #1 $display("deposited=%h", wide);
    #9 $display("recovered=%h", wide);
    $finish;
  end
endmodule

// CHECK: initialized=00000000000000000000000000000007
// CHECK-NEXT: vpi forced=00000000000000000000000000000007
// CHECK-NEXT: deposited=0000000000000000000000000000000x
// CHECK-NEXT: recovered=00000000000000010000000000000002
// QUEUE: @__obelisk_eval_ordered_nba_queue_v1
// QUEUE: call i32 @obelisk_rt_v1_static_nba_commit_ordered

//--- plugin.c
#include "vpi_user.h"

int mutate(int action) {
  vpiHandle handle = vpi_handle_by_name("$root.shared_ordered_nba_vpi.wide", 0);
  if (!handle) return -1;
  s_vpi_vecval bits[4] = {{7, 0}, {0, 0}, {0, 0}, {0, 0}};
  if (action == 3) {
    bits[0].aval = 0xf;
    bits[0].bval = 0xf;
  }
  s_vpi_value value = {vpiVectorVal};
  value.value.vector = bits;
  vpi_put_value(handle, &value, 0, action == 1 ? vpiForceFlag :
                                  action == 2 ? vpiReleaseFlag : vpiNoDelay);
  vpi_release_handle(handle);
  return vpi_chk_error(0) ? -1 : 0;
}

void (*vlog_startup_routines[])(void) = {0};
