// RUN: %split-file %s %t
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -shared -nostdlib \
// RUN:   %t/plugin.c -I%resource_dir/include -o %t/plugin.so
// RUN: obelisk -O0 --vpi=full --native-scheduler=generic \
// RUN:   %t/design.sv %t/plugin.so -o %t/o0
// RUN: %t/o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=full --native-scheduler=generic \
// RUN:   %t/design.sv %t/plugin.so -o %t/o3
// RUN: %t/o3 | FileCheck %s
// RUN: obelisk -O3 --vpi=full --native-scheduler=generic \
// RUN:   -emit-llvm %t/design.sv -o %t/guard.ll
// RUN: FileCheck %s --check-prefix=GUARD < %t/guard.ll
// RUN: FileCheck %s --check-prefix=DYNAMIC < %t/guard.ll
// RUN: obelisk --target=wasm32 -O3 --vpi=full --native-scheduler=generic \
// RUN:   -emit-llvm %t/wasm.sv -o %t/wasm.ll
// RUN: FileCheck %s --check-prefix=GUARD < %t/wasm.ll
// RUN: FileCheck %s --check-prefix=WASM < %t/wasm.ll
// RUN: obelisk -O3 --vpi=full --execution-tier=bytecode \
// RUN:   %t/design.sv %t/plugin.so -o %t/bytecode
// RUN: %t/bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=full -DPARTIAL --mlir-timing %t/design.sv %t/plugin.so -o %t/partial 2> %t/partial.log
// RUN: FileCheck %s --check-prefix=PARTIAL < %t/partial.log
// RUN: %t/partial | FileCheck %s
// PARTIAL: native eligibility: eligible=1 fully_eligible=0 cost_effective=1

// A foreign call may first consume VPI after clean native execution. Force
// must stop subsequent writes, release must recover current continuous/driver
// contributions, and a procedural variable retains its forced value until the
// next write. IEEE 1800-2023 10.6.2 and 38.34.
// CHECK: initial 12 12 34 read=52
// CHECK: select 3 x 5 a x
// CHECK: forced a5 a5 a5
// CHECK: release 56 56 a5
// CHECK: procedural 9a
// CHECK: unknown xxxx0101 xxxx0101 xxxx0101
// CHECK: retained xxxx0101 xxxx0101
// CHECK: recovered 12 12
// GUARD-DAG: call i32 @obelisk_rt_v1_native_state_bind_continuous
// GUARD-DAG: call i32 @obelisk_rt_v1_native_state_bind_specialization
// WASM-DAG: target triple = "wasm32-unknown-emscripten"
// DYNAMIC-DAG: getelementptr i8, ptr @__obelisk_state_value, i64 %
// DYNAMIC-DAG: getelementptr i8, ptr @__obelisk_state_unknown, i64 %

//--- design.sv
module generic_vpi_guard;
`ifdef PARTIAL
  bit clock;
  int ticks[128];
  always #1 clock = ~clock;
  for (genvar i = 0; i < 128; ++i) begin
    always @(posedge clock) ticks[i] <= ticks[i] + 1;
  end
`endif
  logic [7:0] source = 8'h12;
  wire [7:0] resolved = source;
  logic [7:0] continuous;
  assign continuous = source;
  logic [7:0] procedural = 8'h34;
  logic [3:0] choices [0:3] = '{4'h3, 4'hx, 4'h5, 4'ha};
  import "DPI-C" function int disturb(input int action);
  initial begin
    #1;
    $display("initial %h %h %h read=%0d", resolved, continuous,
             procedural, disturb(0));
    $write("select");
    for (int index = disturb(0) - 52; index != 5; ++index)
      $write(" %h", choices[index]);
    $write("\n");
    if (disturb(1) != 0) $fatal(1, "force failed");
    #1 source = 8'h56;
    procedural = 8'h78;
    #1 $display("forced %h %h %h", resolved, continuous, procedural);
    if (disturb(2) != 0) $fatal(1, "release failed");
    #1 $display("release %h %h %h", resolved, continuous, procedural);
    procedural = 8'h9a;
    #1 $display("procedural %h", procedural);
    if (disturb(3) != 0) $fatal(1, "deposit failed");
    #1 $display("unknown %b %b %b", source, resolved, continuous);
    // A deposit changes visibility, not the last continuous contribution.
    if (disturb(4) != 0) $fatal(1, "continuous deposit failed");
    if (disturb(1) != 0) $fatal(1, "second force failed");
    if (disturb(2) != 0) $fatal(1, "second release failed");
    #1 $display("retained %b %b", resolved, continuous);
    source = 8'h12;
    #1;
    if (disturb(4) != 0 || disturb(1) != 0 || disturb(2) != 0)
      $fatal(1, "known contribution recovery failed");
    #1 $display("recovered %h %h", resolved, continuous);
`ifdef PARTIAL
    if (ticks[127] == 0) $fatal(1, "partial island did not run");
`endif
    $finish;
  end
endmodule

//--- plugin.c
#include "vpi_user.h"

int disturb(int action) {
  const char *names[] = {"$root.generic_vpi_guard.resolved",
                        "$root.generic_vpi_guard.continuous",
                        "$root.generic_vpi_guard.procedural"};
  if (action == 0) {
    vpiHandle handle = vpi_handle_by_name((char *)names[2], 0);
    if (!handle) return -1;
    s_vpi_value value = {vpiIntVal};
    vpi_get_value(handle, &value);
    vpi_release_handle(handle);
    return value.value.integer;
  }
  if (action == 3) {
    vpiHandle handle = vpi_handle_by_name("$root.generic_vpi_guard.source", 0);
    if (!handle) return -1;
    s_vpi_vecval bits = {0xf5, 0xf0};
    s_vpi_value value = {vpiVectorVal};
    value.value.vector = &bits;
    vpi_put_value(handle, &value, 0, vpiNoDelay);
    vpi_release_handle(handle);
    return vpi_chk_error(0) ? -1 : 0;
  }
  if (action == 4) {
    vpiHandle handle = vpi_handle_by_name((char *)names[1], 0);
    if (!handle) return -1;
    s_vpi_value value = {vpiIntVal};
    value.value.integer = 0x66;
    vpi_put_value(handle, &value, 0, vpiNoDelay);
    vpi_release_handle(handle);
    return vpi_chk_error(0) ? -1 : 0;
  }
  for (int index = 0; index != 3; ++index) {
    vpiHandle handle = vpi_handle_by_name((char *)names[index], 0);
    if (!handle) return -1;
    s_vpi_value value = {vpiIntVal};
    value.value.integer = 0xa5;
    vpi_put_value(handle, &value, 0,
                  action == 1 ? vpiForceFlag : vpiReleaseFlag);
    vpi_release_handle(handle);
    if (vpi_chk_error(0)) return -1;
  }
  return 0;
}

void (*vlog_startup_routines[])(void) = {0};

//--- wasm.sv
module retained_continuous;
  logic [7:0] source = 8'h12;
  logic [7:0] result;
  assign result = source;
  initial begin
    #1 source = 8'h34;
    #1 $display("%h", result);
  end
endmodule
