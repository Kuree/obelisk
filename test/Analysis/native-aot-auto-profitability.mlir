// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ANALYSIS
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' \
// RUN:   | FileCheck %s --check-prefix=FUSION
// RUN: sed 's/native_scheduler = 0 : i32/native_scheduler = 3 : i32, obelisk.native_scheduler.auto_requested/' %s \
// RUN:   | obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' \
// RUN:   | FileCheck %s --check-prefix=FUSION
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=LOWERING

// A partial schedule has no closed generated execution path. Auto must not pay
// to clone evaluator bodies or emit an AOT plan that calls the generic
// scheduler merely because the design is clockless.

// ANALYSIS: native-aot eligible=true fully=false selected=false periodic=false

// FUSION-NOT: __obelisk_eval_body

// LOWERING-NOT: __obelisk_aot_schedule_plan_v1
// LOWERING-LABEL: llvm.func @main
// LOWERING: llvm.call @obelisk_rt_v1_scheduler_run(
// LOWERING-NOT: llvm.call @obelisk_rt_v1_scheduler_run_aot(

module attributes {
  obelisk.native_scheduler = 0 : i32,
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @auto_profitability {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "native"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "spawner"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "dynamic"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %native = obelisk_sim.spawn @native(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %spawner = obelisk_sim.spawn @spawner(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @native(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.addi %c0, %c1 : i32
      %c3 = arith.addi %c1, %c2 : i32
      %c4 = arith.addi %c2, %c3 : i32
      obelisk_sim.return
    }

    obelisk_sim.func @spawner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %dynamic = obelisk_sim.spawn @dynamic(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @dynamic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.return
    }
  }
}
