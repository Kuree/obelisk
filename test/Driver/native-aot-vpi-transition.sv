// RUN: %split-file %s %t
// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -shared -nostdlib %t/plugin.c \
// RUN:   -I$(obelisk --print-resource-dir)/include \
// RUN:   -Wl,-soname,libnative_aot_vpi_transition.so \
// RUN:   -o %t.dir/lib/libnative_aot_vpi_transition.so
// RUN: cd %t.dir && obelisk --vpi=full --native-scheduler=aot %t/design.sv \
// RUN:   lib/libnative_aot_vpi_transition.so -o bin/simulator
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.dir/bin/simulator 2>&1 \
// RUN:   | FileCheck %s

//--- design.sv
module native_aot_vpi_transition;
  bit clock = 0;
  int seed;
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

// CHECK: seed=41 total=82
// CHECK: aot_node_executions={{([2-9]|[1-9][0-9]+)}}
// CHECK-SAME: aot_fallbacks=0

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
  value.value.integer = 41;
  vpi_put_value(seed, &value, 0, vpiForceFlag);
  vpi_release_handle(seed);
}

void (*vlog_startup_routines[])(void) = {startup, 0};
