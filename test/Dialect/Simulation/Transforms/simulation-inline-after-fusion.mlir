// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=EARLY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion,obelisk-sim-inline{opt-level=3 tiny-cost=64 specialization-cost=192 caller-growth-percent=100 caller-growth-constant=256 design-growth-percent=10 design-growth-constant=1024 max-iterations=2},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=LATE

module {
  simulation.design @late_inline {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 always hierarchy "top.a"
    simulation.code_unit.decl 2 in 0 always hierarchy "top.b"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.work"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    // This identity is public and remains available to hierarchy/VPI metadata
    // after its body has been cloned into the generated fused process.
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %value = arith.constant 0 : i32
      %c = arith.constant 1 : i32
      %v0 = arith.addi %value, %c : i32
      %v1 = arith.addi %v0, %c : i32
      %v2 = arith.addi %v1, %c : i32
      %v3 = arith.addi %v2, %c : i32
      %v4 = arith.addi %v3, %c : i32
      %v5 = arith.addi %v4, %c : i32
      %v6 = arith.addi %v5, %c : i32
      %v7 = arith.addi %v6, %c : i32
      %v8 = arith.addi %v7, %c : i32
      %v9 = arith.addi %v8, %c : i32
      %v10 = arith.addi %v9, %c : i32
      %v11 = arith.addi %v10, %c : i32
      %v12 = arith.addi %v11, %c : i32
      %v13 = arith.addi %v12, %c : i32
      %v14 = arith.addi %v13, %c : i32
      %v15 = arith.addi %v14, %c : i32
      %v16 = arith.addi %v15, %c : i32
      %v17 = arith.addi %v16, %c : i32
      %v18 = arith.addi %v17, %c : i32
      %v19 = arith.addi %v18, %c : i32
      %v20 = arith.addi %v19, %c : i32
      %v21 = arith.addi %v20, %c : i32
      %v22 = arith.addi %v21, %c : i32
      %v23 = arith.addi %v22, %c : i32
      %v24 = arith.addi %v23, %c : i32
      %v25 = arith.addi %v24, %c : i32
      simulation.return %v25 : i32
    }

    simulation.func @root(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = simulation.context.storage %ctx[0] :
        !simulation.ref<!simulation.logic<1>>
      %a = simulation.spawn @a(%ctx, %clock) :
        !simulation.context, !simulation.ref<!simulation.logic<1>>
        -> !simulation.process
      %b = simulation.spawn @b(%ctx, %clock) :
        !simulation.context, !simulation.ref<!simulation.logic<1>>
        -> !simulation.process
      simulation.return
    }

    simulation.func private @a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body :
        !simulation.ref<!simulation.logic<1>>
    ^body:
      %result = simulation.call @work(%ctx) :
        (!simulation.context) -> i32
      cf.br ^wait
    }

    simulation.func private @b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body :
        !simulation.ref<!simulation.logic<1>>
    ^body:
      %result = simulation.call @work(%ctx) :
        (!simulation.context) -> i32
      cf.br ^wait
    }
  }
}

// EARLY: simulation.code_unit.decl 3 in 0 function hierarchy "top.work"
// EARLY-LABEL: simulation.func @work
// EARLY-LABEL: simulation.func private @__obelisk_fused_0
// EARLY: simulation.call @work{{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 1 : i64, continuation = 2 : i32>}
// EARLY: simulation.call @work{{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 2 : i64, continuation = 5 : i32>}

// LATE: simulation.code_unit.decl 3 in 0 function hierarchy "top.work"
// LATE-LABEL: simulation.func @work
// LATE-LABEL: simulation.func private @__obelisk_fused_0
// LATE-NOT: simulation.call @work
// LATE: arith.constant {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 1 : i64, continuation = 2 : i32>} 0 : i32
// LATE: arith.constant {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 2 : i64, continuation = 5 : i32>} 0 : i32
