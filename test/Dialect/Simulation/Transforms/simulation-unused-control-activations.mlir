// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-simplify-bodies{vpi=off}))' | FileCheck %s

// IEEE 1800-2023 9.6.2, 4.3: a named-block activation that no `disable`
// targets and that registers no boundary has no user-visible effect; its
// enter/leave pair is removed. A disabled target keeps its activation.
!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module {
  simulation.design @unused_control {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "unrolled"
    simulation.code_unit.decl 3 in 0 initial hierarchy "reader"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %p0 = simulation.spawn @unrolled(%ctx, %a, %b) : !simulation.context, !ref, !ref -> !simulation.process
      %p1 = simulation.spawn @reader(%ctx, %b) : !simulation.context, !ref -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @unrolled
    // CHECK-NOT: simulation.control.
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.control.
    // CHECK: simulation.suspend.change
    simulation.func private @unrolled(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %c = simulation.control.enter 7
      %v = simulation.ref.load %a : !ref -> !bit
      simulation.ref.store %v to %b {simulation.continuous_store} : !bit, !ref
      simulation.control.leave %c
      simulation.suspend.change %a to ^body : !ref
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %v = simulation.ref.load %b : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %v) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      simulation.return
    }
  }
  simulation.design @disabled_target {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "unrolled"
    simulation.code_unit.decl 3 in 0 initial hierarchy "reader"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %p0 = simulation.spawn @unrolled(%ctx, %a, %b) : !simulation.context, !ref, !ref -> !simulation.process
      %p1 = simulation.spawn @reader(%ctx, %b) : !simulation.context, !ref -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.design @disabled_target
    // CHECK: simulation.control.enter 7
    // CHECK: simulation.control.leave
    simulation.func private @unrolled(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %c = simulation.control.enter 7
      %v = simulation.ref.load %a : !ref -> !bit
      simulation.ref.store %v to %b {simulation.continuous_store} : !bit, !ref
      simulation.control.leave %c
      simulation.suspend.change %a to ^body : !ref
    }
    // CHECK: simulation.control.disable 7
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %v = simulation.ref.load %b : !ref -> !bit
      simulation.control.disable 7 {hierarchical = true}
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %v) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      simulation.return
    }
  }
}
