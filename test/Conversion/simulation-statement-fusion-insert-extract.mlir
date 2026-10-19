// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// Selecting the field just inserted is independent of the base value. The
// integer spelling must simplify in the disposable activation, so the dead
// base load cannot create a false scheduling cycle with the consumer.
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.func private @__obelisk_region_kernel_0_0
module {
  simulation.design @extraction_fusion_bits {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i4 design
    simulation.storage.decl 1 in 0 : i4 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "consumer"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @consumer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i4>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %backedge = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %backedge : !simulation.ref<i8> -> i8
      %new = simulation.ref.load %input : !simulation.ref<i4> -> i4
      %zero = arith.constant 0 : i64
      %updated = simulation.bits.dyn_insert %new into %old at %zero : (i8, i4, i64) -> i8
      %extracted = arith.trunci %updated : i8 to i4
      simulation.ref.store %extracted to %out : i4, !simulation.ref<i4>
      simulation.suspend.any %input, %backedge edges [0, 0] to ^body : !simulation.ref<i4>, !simulation.ref<i8>
    }
    simulation.func @consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %part = simulation.ref.load %input : !simulation.ref<i4> -> i4
      %extended = arith.extui %part : i4 to i8
      simulation.ref.store %extended to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i4>
    }
  }
}
