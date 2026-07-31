// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=EARLY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion,obelisk-sim-inline{opt-level=3 tiny-cost=64 specialization-cost=192 caller-growth-percent=100 caller-growth-constant=256 design-growth-percent=10 design-growth-constant=1024 max-iterations=2},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=LATE

module {
  obelisk_sim.design @late_inline {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "top.a"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "top.b"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.work"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    // This identity is public and remains available to hierarchy/VPI metadata
    // after its body has been cloned into the generated fused process.
    obelisk_sim.func @work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
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
      obelisk_sim.return %v25 : i32
    }

    obelisk_sim.func @root(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %a = obelisk_sim.spawn @a(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      %b = obelisk_sim.spawn @b(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %result = obelisk_sim.call @work(%ctx) :
        (!obelisk_sim.context) -> i32
      cf.br ^wait
    }

    obelisk_sim.func private @b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %result = obelisk_sim.call @work(%ctx) :
        (!obelisk_sim.context) -> i32
      cf.br ^wait
    }
  }
}

// EARLY: obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.work"
// EARLY-LABEL: obelisk_sim.func @work
// EARLY-LABEL: obelisk_sim.func private @__obelisk_fused_0
// EARLY-COUNT-2: obelisk_sim.call @work

// LATE: obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.work"
// LATE-LABEL: obelisk_sim.func @work
// LATE-LABEL: obelisk_sim.func private @__obelisk_fused_0
// LATE-NOT: obelisk_sim.call @work
