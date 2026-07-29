// RUN: %split-file %s %t
// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -shared -nostdlib %t/plugin.c \
// RUN:   -I$(obelisk --print-resource-dir)/include \
// RUN:   -Wl,-soname,libnative_aot_vpi_transition.so \
// RUN:   -o %t.dir/lib/libnative_aot_vpi_transition.so
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -shared -nostdlib %t/readonly.c \
// RUN:   -I$(obelisk --print-resource-dir)/include \
// RUN:   -Wl,-soname,libnative_aot_vpi_readonly.so \
// RUN:   -o %t.dir/lib/libnative_aot_vpi_readonly.so
// RUN: cd %t.dir && obelisk --vpi=full --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_transition.so -o bin/simulator
// RUN: cd %t.dir && obelisk --vpi=full --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_readonly.so -o bin/readonly
// RUN: cd %t.dir && obelisk --vpi=read --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_readonly.so -o bin/readmode
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
// RUN: %t.dir/bin/mixed_nba | FileCheck %s --check-prefix=MIXED-NBA
// RUN: FileCheck %s --check-prefix=MIXED-IR < %t.dir/bin/mixed_nba.mlir

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
// CHECK: obelisk-signal-diagnostics
// CHECK-SAME: scheduler_iterations=1
// CHECK-SAME: aot_node_executions={{([2-9]|[1-9][0-9]+)}}
// CHECK-SAME: aot_fanout_entries=0
// CHECK-SAME: aot_state_fast_paths=0
// CHECK-SAME: aot_state_slow_paths={{[1-9][0-9]*}}
// CHECK-SAME: aot_fallbacks=0

// READONLY: vpi-startup-read={{0|41}}
// READONLY: seed=41 total=82
// READONLY: scheduler_iterations=0
// READONLY-SAME: aot_fanout_entries={{[1-9][0-9]*}}
// READONLY-SAME: aot_state_fast_paths={{[1-9][0-9]*}}
// READONLY-SAME: aot_state_slow_paths=0
// READONLY-SAME: aot_fallbacks=0

// READMODE: vpi-startup-read={{0|41}}
// READMODE: seed=41 total=82
// READMODE: scheduler_iterations=0
// READMODE-SAME: aot_fanout_entries={{[1-9][0-9]*}}
// READMODE-SAME: aot_state_slow_paths=0
// READMODE-SAME: aot_fallbacks=0

// GUARD-DAG: @__obelisk_aot_schedule_plan_v1 = internal constant {{.*}} i32 503, ptr @__obelisk_state_value
// GUARD-DAG: br i1
// GUARD-DAG: load {{.*}} @__obelisk_state_value
// GUARD-DAG: call i32 @obelisk_rt_v1_native_state_load_plane
// GUARD-DAG: store {{.*}} @__obelisk_state_value
// GUARD-DAG: call i32 @obelisk_rt_v1_native_state_store_plane
// GUARD-NOT: @obelisk_rt_v1_static_specialization_guard

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
// MIXED-IR: obelisk_sim.func private @__obelisk_fused_
// MIXED-IR-SAME: obelisk.native.guarded_specialization_body

//--- plugin.c
#include "vpi_user.h"

static void startup(void) {
  vpiHandle seed =
      vpi_handle_by_name("$root.native_aot_vpi_transition.seed", 0);
  if (!seed) {
    vpi_printf("seed-lookup-failed\n");
    return;
  }
  s_vpi_value value = {vpiIntVal};
  value.value.integer = 7;
  vpi_put_value(seed, &value, 0, vpiForceFlag);
  vpi_release_handle(seed);
}

void (*vlog_startup_routines[])(void) = {startup, 0};

//--- readonly.c
#include "vpi_user.h"

static void startup(void) {
  vpiHandle seed =
      vpi_handle_by_name("$root.native_aot_vpi_transition.seed", 0);
  if (!seed) {
    vpi_printf("seed-lookup-failed\n");
    return;
  }
  s_vpi_value value = {vpiIntVal};
  vpi_get_value(seed, &value);
  vpi_printf("vpi-startup-read=%d\n", value.value.integer);
  vpi_release_handle(seed);
}

void (*vlog_startup_routines[])(void) = {startup, 0};
