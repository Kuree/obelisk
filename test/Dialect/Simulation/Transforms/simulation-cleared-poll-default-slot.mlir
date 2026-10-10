// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// A termination poll after a harmless call is cleared by the same pass that
// promotes private state. The cleared branch must be folded before the
// activation-shape proof: the return it guarded is not a process exit, so the
// field-by-field writes still cover the whole private array and its slot
// starts from the default value instead of reloading the previous activation.
!s = !simulation.packed_struct<[#simulation.field<name = "a", type = !simulation.logic<4>, ordinal = 0, packedOffset = 1>, #simulation.field<name = "v", type = !simulation.logic<1>, ordinal = 1, packedOffset = 0>]>
!array = !simulation.unpacked_array<0 : 1 x !s>
module {
  simulation.design @cleared_poll {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : !array design
    simulation.storage.decl 3 in 0 : !array design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "comb"
    simulation.code_unit.decl 2 in 0 function hierarchy "pick"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    // CHECK-LABEL: simulation.func @comb
    // CHECK: simulation.ref.alloc {{.*}}simulation.activation_default_slot
    // CHECK-NOT: simulation.termination.requested
    // CHECK-NOT: simulation.return
    // CHECK: simulation.suspend.change
    simulation.func @comb(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %flag: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %next: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %out: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %start = arith.constant 0 : i32
      %limit = arith.constant 2 : i32
      %step = arith.constant 1 : i32
      %a = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      cf.br ^loop(%start : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      %f = simulation.ref.load %flag : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.call @pick(%ctx, %f) : (!simulation.context, !simulation.logic<1>) -> !simulation.logic<1>
      %poll = simulation.termination.requested %ctx
      cf.cond_br %poll, ^stop, ^store
    ^stop:
      simulation.return
    ^store:
      %wide = arith.extsi %i : i32 to i65
      %e = simulation.ref.array_element %next[%wide] : (!simulation.ref<!array>, i65) -> !simulation.ref<!s>
      %ev = simulation.ref.subelement %e[[1]] : !simulation.ref<!s> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %ev : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %ea = simulation.ref.subelement %e[[0]] : !simulation.ref<!s> -> !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %a to %ea : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %n = arith.addi %i, %step : i32
      cf.br ^loop(%n : i32)
    ^done:
      %all = simulation.ref.load %next : !simulation.ref<!array> -> !array
      simulation.ref.store %all to %out : !array, !simulation.ref<!array>
      simulation.suspend.change %flag to ^resume : !simulation.ref<!simulation.logic<1>>
    ^resume:
      cf.br ^loop(%start : i32)
    }
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!array>
      %copy = simulation.context.storage %ctx[3] : !simulation.ref<!array>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %out : !simulation.ref<!array> -> !array
      simulation.ref.store %value to %copy : !array, !simulation.ref<!array>
      simulation.suspend.change %out to ^body : !simulation.ref<!array>
    }
    simulation.func private @pick(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %f: !simulation.logic<1> {simulation.capture_kind = 1 : i32}) -> !simulation.logic<1> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %n = simulation.logic.unary logical_not %f : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.return %n : !simulation.logic<1>
    }
  }
}
