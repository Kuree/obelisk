// RUN: %split-file %s %t
// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %target_clang -fPIC \
// RUN:   -shared -nostdlib %t/plugin.c \
// RUN:   -I%resource_dir/include \
// RUN:   -Wl,-soname,libnative_aot_vpi_transition.so \
// RUN:   -o %t.dir/lib/libnative_aot_vpi_transition.so
// RUN: %target_clang -fPIC \
// RUN:   -shared -nostdlib %t/readonly.c \
// RUN:   -I%resource_dir/include \
// RUN:   -Wl,-soname,libnative_aot_vpi_readonly.so \
// RUN:   -o %t.dir/lib/libnative_aot_vpi_readonly.so
// RUN: %target_clang -fPIC \
// RUN:   -shared -nostdlib %t/local_read.c \
// RUN:   -I%resource_dir/include \
// RUN:   -Wl,-soname,libnative_aot_vpi_local_read.so \
// RUN:   -o %t.dir/lib/libnative_aot_vpi_local_read.so
// RUN: cd %t.dir && obelisk --vpi=full --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_transition.so -o bin/simulator
// RUN: cd %t.dir && obelisk --vpi=full --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_readonly.so -o bin/readonly
// RUN: cd %t.dir && obelisk --vpi=read --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_readonly.so -o bin/readmode
// RUN: cd %t.dir && obelisk -O3 --vpi=read --native-scheduler=generic \
// RUN:   %t/local_read.sv lib/libnative_aot_vpi_local_read.so \
// RUN:   -o bin/local_read
// RUN: cd %t.dir && obelisk -O2 --vpi=full --native-scheduler=aot \
// RUN:   -emit-llvm %t/design.sv -o bin/guarded.ll
// RUN: cd %t.dir && obelisk -O2 --vpi=full --native-scheduler=aot \
// RUN:   %t/mixed_nba.sv -o bin/mixed_nba
// RUN: obelisk -O2 --vpi=full -emit-sim %t/mixed_nba.sv \
// RUN:   -o %t.dir/bin/mixed_nba.mlir
// RUN: FileCheck %s --check-prefix=GUARD < %t.dir/bin/guarded.ll
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.dir/bin/simulator 2>&1 \
// RUN:   | FileCheck %s
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.dir/bin/readonly 2>&1 \
// RUN:   | FileCheck %s --check-prefix=READONLY
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.dir/bin/readmode 2>&1 \
// RUN:   | FileCheck %s --check-prefix=READMODE
// RUN: %t.dir/bin/local_read | FileCheck %s --check-prefix=LOCAL-READ
// RUN: %t.dir/bin/mixed_nba | FileCheck %s --check-prefix=MIXED-NBA
// RUN: FileCheck %s --check-prefix=MIXED-IR < %t.dir/bin/mixed_nba.mlir
// RUN: cd %t.dir && obelisk -O3 --vpi=full --native-scheduler=generic \
// RUN:   %t/design.sv lib/libnative_aot_vpi_transition.so -o bin/generic
// RUN: %t.dir/bin/generic | FileCheck %s --check-prefix=GENERIC
// RUN: obelisk -O3 --vpi=full --native-scheduler=generic -emit-llvm \
// RUN:   %t/design.sv -o - | FileCheck %s --check-prefix=GENERIC-IR
// RUN: cd %t.dir && obelisk -O3 --vpi=full --native-scheduler=generic \
// RUN:   %t/local_read.sv lib/libnative_aot_vpi_local_read.so -o bin/local_full
// RUN: %t.dir/bin/local_full | FileCheck %s --check-prefix=LOCAL-READ

//--- design.sv
module native_aot_vpi_transition;
  bit clock = 0;
  int seed = 41;
  int total = 0;

  always @(posedge clock)
    total <= total + seed;

  initial begin
    #1 clock = 1;
    #1 clock = 0;
    #1 clock = 1;
    #1 clock = 0;
    #1;
    $display("seed=%0d total=%0d", seed, total);
    $finish;
  end
endmodule

// CHECK: seed=7 total=14
// GENERIC: seed=7 total=14
// GENERIC-IR: call i32 @obelisk_rt_v1_native_state_bind_specialization
// CHECK: obelisk-signal-diagnostics
// A startup force remains active until a vpiReleaseFlag operation.  The
// generated schedule therefore keeps ownership in the fine scheduler, whose
// direct native waits still use its candidate set. Their validated publication
// latches now establish readiness without rescanning serialized wait records.
// CHECK-SAME: readiness_calls=0
// CHECK-SAME: candidate_scans={{[1-9][0-9]*}}
// CHECK-SAME: scheduler_iterations={{[1-9][0-9]*}}
// CHECK-SAME: aot_node_executions=0
// CHECK-SAME: aot_fanout_entries=0
// Ordered runtime publications keep both staged updates separate.
// CHECK-SAME: aot_nba_stages=2
// CHECK-SAME: aot_nba_commits=0
// CHECK-SAME: aot_state_slow_paths={{[1-9][0-9]*}}
// CHECK-SAME: aot_fallbacks=0

// READONLY: vpi-startup-read={{0|41}}
// READONLY: seed=41 total=82
// Startup and fragment arbitration now use the shared loop. Read demand still
// preserves direct fanout and guarded native state with no slow accesses.
// READONLY: scheduler_iterations=18
// READONLY-SAME: aot_node_executions=10
// READONLY-SAME: aot_fanout_entries={{[1-9][0-9]*}}
// READONLY-SAME: aot_state_fast_paths={{[1-9][0-9]*}}
// READONLY-SAME: aot_state_slow_paths=0
// READONLY-SAME: aot_fallbacks=0

// READMODE: vpi-startup-read={{0|41}}
// READMODE: seed=41 total=82
// READMODE: scheduler_iterations=18
// READMODE-SAME: aot_node_executions=10
// READMODE-SAME: aot_fanout_entries={{[1-9][0-9]*}}
// READMODE-SAME: aot_state_slow_paths=0
// READMODE-SAME: aot_fallbacks=0

// LOCAL-READ: local=14 published=14

// GUARD-DAG: @__obelisk_aot_schedule_plan_v1 = internal constant {{.*}} i32 895, ptr @__obelisk_state_value
// GUARD-DAG: br i1
// GUARD-DAG: load {{.*}} @__obelisk_state_value
// GUARD-DAG: call i32 @obelisk_rt_v1_native_state_load_plane
// GUARD-DAG: store {{.*}} @__obelisk_state_value
// GUARD-DAG: call i32 @obelisk_rt_v1_native_state_store_plane
// Cold root initialization may still query the writable-VPI specialization
// guard. Generated Tier-1/Tier-2 closures are checked separately by the eval
// call-closure verifier and emitted-LLVM scheduler tests.

//--- mixed_nba.sv
module native_aot_vpi_mixed_nba;
  bit clock = 0;
  bit [255:0] state = 0;
  bit witness = 0;

  always @(posedge clock) begin
    state[127:0] <= 128'h01234567_89abcdef_fedcba98_76543210;
    state[31:0] <= 32'hdeadbeef;
  end

  // Keep a second adjacent actor on the same sensitivity so this regression
  // exercises fused activation handover, not only ordinary guarded lowering.
  always @(posedge clock)
    witness <= ~witness;

  initial begin
    #1 clock = 1;
    #1;
    $display("mixed=%032h", state[127:0]);
    $finish;
  end
endmodule

// MIXED-NBA: mixed=0123456789abcdeffedcba98deadbeef
// MIXED-IR: simulation.func private @__obelisk_fused_
// MIXED-IR-SAME: schedule.native.guarded_specialization_body

//--- plugin.c
#include "vpi_user.h"

static PLI_INT32 end_compile(p_cb_data callback) {
  vpiHandle seed =
      vpi_handle_by_name("$root.native_aot_vpi_transition.seed", 0);
  if (!seed) {
    vpi_printf("seed-lookup-failed\n");
    return 0;
  }
  s_vpi_value value = {vpiIntVal};
  value.value.integer = 7;
  vpi_put_value(seed, &value, 0, vpiForceFlag);
  vpi_release_handle(seed);
  return 0;
}

static void startup(void) {
  static s_cb_data callback = {cbEndOfCompile, end_compile};
  vpi_register_cb(&callback);
}

void (*vlog_startup_routines[])(void) = {startup, 0};

//--- local_read.sv
module native_aot_vpi_local_read;
  bit clock = 0;
  int published = 0;

  import "DPI-C" function int read_local();

  always @(posedge clock) begin : worker
    int next;
    next = published + 7;
    published <= next;
  end

  initial begin
    repeat (2) begin
      #1 clock = 1;
      #1 clock = 0;
    end
    #1;
    $display("local=%0d published=%0d", read_local(), published);
    $finish;
  end
endmodule

//--- local_read.c
#include "vpi_user.h"

int read_local(void) {
  vpiHandle local = vpi_handle_by_name(
      "$root.native_aot_vpi_local_read.worker.next", 0);
  if (!local)
    return -1;
  s_vpi_value value = {vpiIntVal};
  vpi_get_value(local, &value);
  vpi_release_handle(local);
  return value.value.integer;
}

void (*vlog_startup_routines[])(void) = {0};

//--- readonly.c
#include "vpi_user.h"

static PLI_INT32 end_compile(p_cb_data callback) {
  vpiHandle seed =
      vpi_handle_by_name("$root.native_aot_vpi_transition.seed", 0);
  if (!seed) {
    vpi_printf("seed-lookup-failed\n");
    return 0;
  }
  s_vpi_value value = {vpiIntVal};
  vpi_get_value(seed, &value);
  vpi_printf("vpi-startup-read=%d\n", value.value.integer);
  vpi_release_handle(seed);
  return 0;
}

static void startup(void) {
  static s_cb_data callback = {cbEndOfCompile, end_compile};
  vpi_register_cb(&callback);
}

void (*vlog_startup_routines[])(void) = {startup, 0};
