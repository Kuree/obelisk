// REQUIRES: native-elf
// RUN: %split-file %s %t
// RUN: %target_clang -fPIC -shared -nostdlib %t/plugin.c -I%resource_dir/include -o %t/plugin.so
// RUN: obelisk -O3 --vpi=full --native-scheduler=eval --mlir-timing %t/design.sv %t/plugin.so -o %t/eval 2> %t.timing
// RUN: FileCheck %s --check-prefix=PLAN < %t.timing
// RUN: obelisk -O3 --vpi=full --execution-tier=bytecode %t/design.sv %t/plugin.so -o %t/bytecode
// RUN: %t/eval > %t/eval.out
// RUN: %t/bytecode | diff -u %t/eval.out -
// RUN: FileCheck %s < %t/eval.out

// LRM 4.9.4/10.4.2: the clock captures its operands before this Active
// foreign call. A deposit into unwritten bits survives the pending partial
// NBA; the force suppresses the captured count update without replaying its
// producer. Release and X/Z recovery affect only the dependent work
// (10.6.2, 38.34), while the independent counter consumes each NBA once.
// Opaque source DPI retains canonical pending work. The local-write service
// and scoped Tier-1 routing are tested separately with closed source effects.
// PLAN: obelisk eval transaction source roots: 0/3
// CHECK: vpi forced=7 untouched=1 q=ab01
// CHECK-NEXT: deposited=ab0x
// CHECK-NEXT: released=8 untouched=2 q=abxx
// CHECK-NEXT: recovered=21 untouched=3 q=ab11

//--- design.sv
module pending_vpi_write;
  bit clk = 0;
  int count = 0;
  int untouched = 0;
  logic [15:0] q = 16'h1200;
  import "DPI-C" function int mutate(input int action);
  always #5 clk = ~clk;
  always @(posedge clk) count <= count + 1;
  always @(posedge clk) untouched <= untouched + 1;
  always @(posedge clk) q[7:0] <= q[7:0] + 1;
  initial begin
    automatic string label = "vpi";
    @(posedge clk);
    #0; // Inactive: every Active NBA producer has captured its operands.
    if (mutate(1)) $fatal(1, "force/deposit failed");
    #1 $display("%s forced=%0d untouched=%0d q=%h", label, count, untouched, q);
    if (mutate(2)) $fatal(1, "release/X deposit failed");
    $display("deposited=%h", q);
    @(posedge clk);
    #1 $display("released=%0d untouched=%0d q=%h", count, untouched, q);
    if (mutate(3)) $fatal(1, "recovery deposit failed");
    @(posedge clk);
    #1 $display("recovered=%0d untouched=%0d q=%h", count, untouched, q);
    $finish;
  end
endmodule

//--- plugin.c
#include "vpi_user.h"

static int put(const char *name, int value, int unknown, int mode) {
  vpiHandle handle = vpi_handle_by_name((PLI_BYTE8 *)name, 0);
  if (!handle) return -1;
  s_vpi_vecval bits = {value, unknown};
  s_vpi_value data = {vpiVectorVal};
  data.value.vector = &bits;
  vpi_put_value(handle, &data, 0, mode);
  vpi_release_handle(handle);
  return vpi_chk_error(0) ? -1 : 0;
}
int mutate(int action) {
  const char *count = "$root.pending_vpi_write.count";
  const char *q = "$root.pending_vpi_write.q";
  if (action == 1)
    return put(count, 7, 0, vpiForceFlag) || put(q, 0xab0a, 0, vpiNoDelay);
  if (action == 2)
    return put(count, 0, 0, vpiReleaseFlag) || put(q, 0xab0f, 0xf, vpiNoDelay);
  return put(count, 20, 0, vpiNoDelay) || put(q, 0xab10, 0, vpiNoDelay);
}
void (*vlog_startup_routines[])(void) = {0};
