// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-simplify-bodies{vpi=read}))' | FileCheck %s --check-prefix=READ

// IEEE 1800-2023 4.3: a process that only reads and waits has no user-visible
// effect, so it is erased with its spawn when no VPI observer exists. Processes
// that write observed state, report or wait for time stay.
!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module {
  simulation.design @effect_free {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "waiter"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "copy"
    simulation.code_unit.decl 4 in 0 always hierarchy "reporter"
    simulation.code_unit.decl 5 in 0 always hierarchy "timed"
    // CHECK-LABEL: simulation.func @root
    // CHECK-NOT: simulation.spawn @waiter
    // CHECK: simulation.spawn @copy
    // CHECK: simulation.spawn @reporter
    // CHECK: simulation.spawn @timed
    // READ-LABEL: simulation.func @root
    // READ: simulation.spawn @waiter
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %p0 = simulation.spawn @waiter(%ctx, %a) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @copy(%ctx, %a, %b) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @reporter(%ctx, %b) : !simulation.context, !ref -> !simulation.process
      %p3 = simulation.spawn @timed(%ctx, %a) : !simulation.context, !ref -> !simulation.process
      simulation.return
    }
    // CHECK-NOT: simulation.func private @waiter
    // READ: simulation.func private @waiter
    simulation.func private @waiter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %a : !ref -> !bit
      %n = simulation.logic.unary logical_not %v : (!bit) -> !bit
      simulation.suspend.change %a to ^body : !ref
    }
    // CHECK: simulation.func private @copy
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %a : !ref -> !bit
      simulation.ref.store %v to %b {simulation.continuous_store} : !bit, !ref
      simulation.suspend.change %a to ^body : !ref
    }
    // CHECK: simulation.func private @reporter
    simulation.func private @reporter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %b : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %v) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      simulation.suspend.change %b to ^body : !ref
    }
    // A delay keeps the simulation alive; the process stays.
    // CHECK: simulation.func private @timed
    simulation.func private @timed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 5
      cf.br ^body
    ^body:
      %v = simulation.ref.load %a : !ref -> !bit
      simulation.suspend.delay %delay to ^body
    }
  }
}
