// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s \
// RUN:   -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=EVAL < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > /dev/null 2> %t.diag
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// A large event-driven island can use generated eval without a structural
// periodic clock. The string actor remains outside the admitted island.
module native_tier1_partial_clockless_admission;
  logic [7:0] stimulus = 0;
  logic [7:0] value [0:255];
  string message;

  for (genvar i = 0; i < 256; i++) begin : g
    always_comb value[i] = stimulus + i;
  end

  initial begin
    message = "probe";
    #1 stimulus = 3;
    #1 $display("%s %d", message, value[255]);
    $finish;
  end
endmodule

// ADMISSION: native eligibility: eligible=1 fully_eligible=0 cost_effective=1
// EVAL: @__obelisk_eval_dispatch_v1
// EVAL: @__obelisk_aot_schedule_plan_v1
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_node_executions={{[1-9][0-9]*}}
// DIAG-SAME: aot_fallbacks=0
