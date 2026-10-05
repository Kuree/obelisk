// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-alias-port-connections{vpi=off}))' | FileCheck %s

module {
// LRM 6.8 also applies to reads in the initializer call closure.
// CHECK-LABEL: simulation.design @initializer_call
// CHECK: simulation.spawn @copy
  simulation.design @initializer_call {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.code_unit.decl 5 in 0 function hierarchy "read_sink"
    simulation.code_unit.decl 6 in 0 function hierarchy "initialize"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      simulation.call @initialize(%ctx) : (!simulation.context) -> ()
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
    simulation.func private @read_sink(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<4> attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %v = simulation.ref.load %d : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.return %v : !simulation.logic<4>
    }
    simulation.func private @initialize(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %v = simulation.call @read_sink(%ctx) : (!simulation.context) -> !simulation.logic<4>
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %v to %s : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

// One symbol use does not prove that a copy executes unconditionally.
// CHECK-LABEL: simulation.design @conditional_spawn
// CHECK: simulation.spawn @copy
  simulation.design @conditional_spawn {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %cond = arith.constant false
      cf.cond_br %cond, ^copy, ^done
    ^copy:
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      cf.br ^done
    ^done:
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

// One symbol use in a loop can create multiple processes.
// CHECK-LABEL: simulation.design @loop_spawn
// CHECK: simulation.spawn @copy
  simulation.design @loop_spawn {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      cf.br ^copy
    ^copy:
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %cond = arith.constant false
      cf.cond_br %cond, ^copy, ^done
    ^done:
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

// LRM 4.5 does not permit Active copies during the Reactive region set.
// CHECK-LABEL: simulation.design @reactive_writer
// CHECK: simulation.spawn @copy
  simulation.design @reactive_writer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {home_region = 10 : i32, entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

// Externally observed storage retains its identity.
// CHECK-LABEL: simulation.design @visible_sink
// CHECK: simulation.spawn @copy
  simulation.design @visible_sink {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 1 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

// Pre-existing projected captures require composition and stay conservative.
// CHECK-LABEL: simulation.design @viewed_sink
// CHECK: simulation.spawn @copy
  simulation.design @viewed_sink {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64, simulation.descriptor_root_type = !simulation.logic<4>, simulation.descriptor_low = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

// LRM 10.6.2: forcing the source pins the connection too.
// CHECK-LABEL: simulation.design @forced_source
// CHECK: simulation.spawn @copy
  simulation.design @forced_source {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.override %out = %c assign false : !simulation.ref<!simulation.logic<4>>, !simulation.logic<4>
      simulation.return
    }
  }
}
