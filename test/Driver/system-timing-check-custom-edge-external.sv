// RUN: %split-file %s %t
// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -shared -nostdlib %t/plugin.c -I%resource_dir/include -Wl,-soname,libcustom_edge_external.so -o %t.dir/lib/libcustom_edge_external.so
// RUN: cd %t.dir && obelisk -O0 --vpi=full --native-scheduler=generic %t/design.sv lib/libcustom_edge_external.so -o bin/native
// RUN: cd %t.dir && obelisk -O3 --vpi=full --native-scheduler=auto --mlir-timing %t/design.sv lib/libcustom_edge_external.so -o bin/aot 2> %t.auto.timing
// RUN: FileCheck %s --check-prefix=ELIGIBILITY < %t.auto.timing
// RUN: %t.dir/bin/native | FileCheck %s
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.dir/bin/aot 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=AOT

//--- design.sv
`timescale 1ns / 1ps

module system_timing_check_custom_edge_external;
  logic data = 0;
  logic ref_zero = 0, ref_one = 1, ref_x = 1'bx, ref_z = 1'bz;
  logic [519:0] wide_deposit = 0;
  wire [519:0] wide_force = '0;
  logic call_first = 0, call_deposit = 0, call_force = 0, call_release = 0;
  logic witness = 0;
  reg zero_notifier = 0, one_notifier = 0;
  reg x_notifier = 0, z_notifier = 0;
  reg deposit_notifier = 0, force_notifier = 0;

  import "DPI-C" function void external_first_transitions();
  import "DPI-C" function void external_partial_deposit();
  import "DPI-C" function void external_partial_force();
  import "DPI-C" function void external_partial_release();

  // Foreign reentrancy stays runtime-owned. Automatic admission still requires
  // whole-design closure, so these boundaries select the descriptor-driven
  // path even though other actors are statically eligible.
  always @(posedge call_first) external_first_transitions();
  always @(posedge call_deposit) external_partial_deposit();
  always @(posedge call_force) external_partial_force();
  always @(posedge call_release) external_partial_release();
  always @(posedge data) witness <= ~witness;

  specify
    // IEEE 1800-2017 31.5 retains the first transition from every four-state
    // value; 31.8 coalesces a partial packed publication into one occurrence.
    $setup(posedge data, edge [01] ref_zero, 3, zero_notifier);
    $setup(posedge data, edge [10] ref_one, 3, one_notifier);
    $setup(posedge data, edge [x1] ref_x, 3, x_notifier);
    $setup(posedge data, edge [z0] ref_z, 3, z_notifier);
    $setup(posedge data, edge [01] wide_deposit, 3, deposit_notifier);
    $setup(posedge data, edge [01, 10] wide_force, 3, force_notifier);
  endspecify

  initial begin
    #1 data = 1;
    #1 call_first = 1;
    #0.001;
    $display("external-first %b %b %b %b", zero_notifier, one_notifier,
             x_notifier, z_notifier);

    data = 0; #1 data = 1; #1 call_deposit = 1; #0.001;
    $display("external-deposit %b value=%b", deposit_notifier,
             wide_deposit[257]);

    data = 0; #1 data = 1; #1 call_force = 1; #0.001;
    $display("external-force %b value=%b", force_notifier, wide_force[255]);
    data = 0; #1 data = 1; #1 call_release = 1; #0.001;
    $display("external-release %b value=%b", force_notifier, wide_force[255]);
    $finish;
  end
endmodule

// CHECK: external-first 1 1 1 1
// CHECK: external-deposit 1 value=1
// CHECK: external-force 1 value=1
// CHECK: external-release 0 value=0
// AOT: obelisk-signal-diagnostics
// AOT-SAME: aot_node_executions=0
// AOT-SAME: aot_fallbacks=0
// ELIGIBILITY: obelisk native eligibility: eligible=1 fully_eligible=0 cost_effective=0
// ELIGIBILITY: obelisk native boundary: DPI reentrancy is present

//--- plugin.c
#include "vpi_user.h"

static vpiHandle lookup(const char *suffix) {
  return vpi_handle_by_name((PLI_BYTE8 *)suffix, 0);
}

static void write_bin(const char *path, char *spelling, int flag) {
  vpiHandle handle = lookup(path);
  if (!handle) {
    vpi_printf("external-lookup-failed %s\n", path);
    return;
  }
  s_vpi_value value = {vpiBinStrVal};
  value.value.str = (PLI_BYTE8 *)spelling;
  vpi_put_value(handle, &value, 0, flag);
  vpi_release_handle(handle);
}

static void write_one_hot(const char *path, unsigned bit, int flag) {
  vpiHandle handle = lookup(path);
  if (!handle) {
    vpi_printf("external-lookup-failed %s\n", path);
    return;
  }
  s_vpi_vecval words[17] = {{0, 0}};
  words[bit / 32].aval = 1u << (bit % 32);
  s_vpi_value value = {vpiVectorVal};
  value.value.vector = words;
  vpi_put_value(handle, &value, 0, flag);
  vpi_release_handle(handle);
}

void external_first_transitions(void) {
  write_bin("$root.system_timing_check_custom_edge_external.ref_zero", "1",
            vpiNoDelay);
  write_bin("$root.system_timing_check_custom_edge_external.ref_one", "0",
            vpiNoDelay);
  write_bin("$root.system_timing_check_custom_edge_external.ref_x", "1",
            vpiNoDelay);
  write_bin("$root.system_timing_check_custom_edge_external.ref_z", "0",
            vpiNoDelay);
}

void external_partial_deposit(void) {
  write_one_hot("$root.system_timing_check_custom_edge_external.wide_deposit",
                257, vpiNoDelay);
}

void external_partial_force(void) {
  write_one_hot("$root.system_timing_check_custom_edge_external.wide_force",
                255, vpiForceFlag);
}

void external_partial_release(void) {
  vpiHandle handle =
      lookup("$root.system_timing_check_custom_edge_external.wide_force");
  if (!handle) {
    vpi_printf("external-release-lookup-failed\n");
    return;
  }
  vpi_put_value(handle, 0, 0, vpiReleaseFlag);
  vpi_release_handle(handle);
}

void (*vlog_startup_routines[])(void) = {0};
