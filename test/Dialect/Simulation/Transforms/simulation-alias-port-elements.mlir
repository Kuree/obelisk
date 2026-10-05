// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-alias-port-connections{vpi=off elements=true}))' | FileCheck %s

module {
// Distinct child outputs may not claim the same parent element.
// CHECK-LABEL: simulation.design @overlap
// CHECK: simulation.spawn @copy(
// CHECK: simulation.spawn @copy2(
  simulation.design @overlap {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.code_unit.decl 4 in 0 port_output hierarchy "copy2"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %i = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      %1 = simulation.spawn @child(%ctx, %i, %s) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %2 = simulation.spawn @copy2(%ctx, %i, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @copy2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

// Disjoint output elements can share one parent root.
// CHECK-LABEL: simulation.design @disjoint
// CHECK-NOT: simulation.spawn @copy
// CHECK: simulation.func private @child
  simulation.design @disjoint {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.code_unit.decl 4 in 0 port_output hierarchy "copy2"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %i = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      %1 = simulation.spawn @child(%ctx, %i, %s) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %2 = simulation.spawn @copy2(%ctx, %i, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @copy2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[1]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

// Unknown array indices overlap every element of the parent.
// CHECK-LABEL: simulation.design @dynamic_writer
// CHECK: simulation.spawn @copy
  simulation.design @dynamic_writer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.code_unit.decl 4 in 0 function hierarchy "dynamic_writer"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %i = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      %1 = simulation.spawn @child(%ctx, %i, %s) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %idx = arith.constant 0 : i64
      simulation.call @dynamic_writer(%ctx, %d, %idx) : (!simulation.context, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>, i64) -> ()
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @dynamic_writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %index: i64 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %e = simulation.ref.array_element %dst[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>, i64) -> !simulation.ref<!simulation.logic<1>>
      %v = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }

// External captures cannot have their signature and body lifted.
// CHECK-LABEL: simulation.design @external_capture
// CHECK: simulation.spawn @copy
  simulation.design @external_capture {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.code_unit.decl 4 in 0 function hierarchy "external"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %i = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      %1 = simulation.spawn @child(%ctx, %i, %s) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @external(!simulation.context {simulation.capture_kind = 0 : i32}, !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64}
  }
}
