// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ANALYSIS
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' \
// RUN:   | sed 's/cost = 5/cost = 1500/' \
// RUN:   | obelisk-opt - -o /dev/null \
// RUN:     --pass-pipeline='builtin.module(test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=LARGE
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' \
// RUN:   | FileCheck %s --check-prefix=FUSION
// RUN: sed 's/native_scheduler = 0 : i32/native_scheduler = 3 : i32, schedule.native_scheduler.auto_requested/' %s \
// RUN:   | obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' \
// RUN:   | FileCheck %s --check-prefix=FUSION
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=LOWERING

// A tiny clockless partial schedule should not pay to clone evaluator bodies.
// With enough native graph cost, the shared AOT node loop can instead pay for
// generated eval even without a structural periodic-clock candidate.

// ANALYSIS: native-aot eligible=true fully=false selected=false periodic=false
// LARGE: native-aot eligible=true fully=false selected=true periodic=false cost=1502/1504

// FUSION-NOT: __obelisk_eval_body

// LOWERING-NOT: __obelisk_aot_schedule_plan_v1
// LOWERING-LABEL: llvm.func @main
// LOWERING: llvm.call @obelisk_rt_v1_scheduler_run(
// LOWERING-NOT: llvm.call @obelisk_rt_v1_scheduler_run_aot(

module attributes {
  schedule.native_scheduler = 0 : i32,
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @auto_profitability {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "native"
    simulation.code_unit.decl 3 in 0 initial hierarchy "spawner"
    simulation.code_unit.decl 4 in 0 initial hierarchy "dynamic"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %native = simulation.spawn @native(%ctx) :
          !simulation.context -> !simulation.process
      %spawner = simulation.spawn @spawner(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @native(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.addi %c0, %c1 : i32
      %c3 = arith.addi %c1, %c2 : i32
      %c4 = arith.addi %c2, %c3 : i32
      simulation.return
    }

    simulation.func @spawner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %dynamic = simulation.spawn @dynamic(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @dynamic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
  }
}
