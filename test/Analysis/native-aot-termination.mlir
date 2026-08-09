// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ANALYSIS
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=LOWERING

// Stop and fatal are global scheduler-control boundaries. Even when enough
// other actors make a partial native schedule cost-effective, Auto must use
// the generic scheduler so the terminating actor cannot bypass region commits.

// ANALYSIS: native-aot eligible=false fully=false selected=false
// ANALYSIS-NEXT: reason fatal or stop control requires generic ordering
// ANALYSIS-NOT: actor

// LOWERING-NOT: __obelisk_aot_schedule_plan_v1
// LOWERING-LABEL: llvm.func @main
// LOWERING: llvm.call @obelisk_rt_v1_scheduler_run(
// LOWERING-NOT: llvm.call @obelisk_rt_v1_scheduler_run_aot(

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @termination {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "termination.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "termination.native0"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "termination.native1"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "termination.native2"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "termination.native3"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "termination.stop"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %native0 = obelisk_sim.spawn @native0(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %native1 = obelisk_sim.spawn @native1(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %native2 = obelisk_sim.spawn @native2(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %native3 = obelisk_sim.spawn @native3(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %stop = obelisk_sim.spawn @stop(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @native0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @native1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @native2(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @native3(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @stop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %verbosity = arith.constant 1 : i32
      obelisk_sim.stop %ctx, %verbosity
      obelisk_sim.return
    }
  }
}
