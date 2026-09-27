// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ANALYSIS
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=LOWERING

// Stop/fatal share the native scheduler's finish transaction. Merely having a
// termination operation must not force unrelated actors onto the generic path.

// ANALYSIS: native-aot eligible=true fully=true selected=true
// ANALYSIS-NOT: reason
// ANALYSIS: actor 5 @stop

// LOWERING: __obelisk_aot_schedule_plan_v1
// LOWERING-LABEL: llvm.func @main
// LOWERING: llvm.call @obelisk_rt_v1_scheduler_run_aot(

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @termination {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "termination.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "termination.native0"
    simulation.code_unit.decl 3 in 0 initial hierarchy "termination.native1"
    simulation.code_unit.decl 4 in 0 initial hierarchy "termination.native2"
    simulation.code_unit.decl 5 in 0 initial hierarchy "termination.native3"
    simulation.code_unit.decl 6 in 0 initial hierarchy "termination.stop"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %native0 = simulation.spawn @native0(%ctx) :
          !simulation.context -> !simulation.process
      %native1 = simulation.spawn @native1(%ctx) :
          !simulation.context -> !simulation.process
      %native2 = simulation.spawn @native2(%ctx) :
          !simulation.context -> !simulation.process
      %native3 = simulation.spawn @native3(%ctx) :
          !simulation.context -> !simulation.process
      %stop = simulation.spawn @stop(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @native0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }

    simulation.func @native1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }

    simulation.func @native2(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }

    simulation.func @native3(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      simulation.return
    }

    simulation.func @stop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %verbosity = arith.constant 1 : i32
      simulation.stop %ctx, %verbosity
      simulation.return
    }
  }
}
