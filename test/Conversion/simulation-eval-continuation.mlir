// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// Generated eval bodies retain a stable continuation in the normalized
// internal continuation namespace. Source diagnostic id 42 is intentionally
// remapped to dense scheduler continuation 1; the scheduler must not recover
// that identity from an operation pointer after fusion or CFG cleanup, and
// eligibility must not depend on a unit_N symbol spelling.
module attributes {obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @eval_continuation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "test.project_clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %process = obelisk_sim.spawn @project_clock(%ctx, %input, %output) :
          !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i1>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @project_clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %output: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.ref.store %value to %output : i1, !obelisk_sim.ref<i1>
      obelisk_sim.suspend.change %input to ^body
          {site = #obelisk_sim.continuation<id = 42>} : !obelisk_sim.ref<i1>
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @project_clock.__obelisk_eval_body_0
// CHECK-SAME: obelisk.eval.borrowed_captures
// CHECK-SAME: obelisk.eval.continuation = 1 : i32
// CHECK-SAME: obelisk.eval.raw_captures
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.return
