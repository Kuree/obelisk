// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,test-obelisk-repeatable-activations))' -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,test-obelisk-repeatable-activations))' -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,test-obelisk-repeatable-activations))' -o /dev/null 2>&1 | FileCheck %s --check-prefix=FULL --implicit-check-not=' = 1'

// LRM 4.3 permits redundant evaluation only with identical observable effects.
// Complete sensitivity and exclusive, single writes establish that unchanged
// inputs produce no update event (4.9.1, 9.2.2.2, 9.4.2). In particular, the
// final value alone cannot certify an activation with intermediate writes.
// FULL: repeatable @root = 0
!ref = !simulation.ref<i8>
!flag = !simulation.ref<i1>
module {
  simulation.design @repeatable {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.storage.decl 11 in 0 : i8 design
    simulation.code_unit.decl 11 in 0 continuous hierarchy "multiple_instances"
    simulation.storage.decl 12 in 0 : i8 design
    simulation.code_unit.decl 12 in 0 continuous hierarchy "loop_instance"
    simulation.code_unit.decl 100 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 1 in 0 continuous hierarchy "copy"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "diamond"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "unwatched"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "incomplete"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "repeated"
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "stateful"
    simulation.code_unit.decl 7 in 0 always hierarchy "general_always"
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "captured_snapshot"
    simulation.code_unit.decl 9 in 0 always_comb hierarchy "conflicting_a"
    simulation.code_unit.decl 10 in 0 always_comb hierarchy "conflicting_b"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 100 : i64} {
      %p_copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process
      %p_diamond = simulation.spawn @diamond(%ctx) : !simulation.context -> !simulation.process
      %p_unwatched = simulation.spawn @unwatched(%ctx) : !simulation.context -> !simulation.process
      %p_incomplete = simulation.spawn @incomplete(%ctx) : !simulation.context -> !simulation.process
      %p_repeated = simulation.spawn @repeated(%ctx) : !simulation.context -> !simulation.process
      %p_stateful = simulation.spawn @stateful(%ctx) : !simulation.context -> !simulation.process
      %p_general_always = simulation.spawn @general_always(%ctx) : !simulation.context -> !simulation.process
      %p_captured_snapshot = simulation.spawn @captured_snapshot(%ctx) : !simulation.context -> !simulation.process
      %p_conflicting_a = simulation.spawn @conflicting_a(%ctx) : !simulation.context -> !simulation.process
      %p_conflicting_b = simulation.spawn @conflicting_b(%ctx) : !simulation.context -> !simulation.process
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      %first = simulation.spawn @multiple_instances(%ctx, %one) : !simulation.context, i8 -> !simulation.process
      %second = simulation.spawn @multiple_instances(%ctx, %two) : !simulation.context, i8 -> !simulation.process
      %zero = arith.constant 0 : i8
      cf.br ^spawn_loop(%zero : i8)
    ^spawn_loop(%iteration: i8):
      %loop_actor = simulation.spawn @loop_instance(%ctx, %iteration) : !simulation.context, i8 -> !simulation.process
      %next = arith.addi %iteration, %one : i8
      %more = arith.cmpi ult, %next, %two : i8
      cf.cond_br %more, ^spawn_loop(%next : i8), ^done
    ^done:
      simulation.return
    }
    // CHECK: repeatable @copy = 1
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[2] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      simulation.ref.store %v to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @diamond = 1
    simulation.func @diamond(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[3] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      %test = simulation.ref.load %flag : !flag -> i1
      cf.cond_br %test, ^yes, ^no
    ^yes:
      simulation.ref.store %v to %out : i8, !ref
      cf.br ^wait
    ^no:
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %out : i8, !ref
      cf.br ^wait
    ^wait:
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @unwatched = 0
    simulation.func @unwatched(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[4] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      simulation.ref.store %v to %out : i8, !ref
      simulation.suspend.change %flag to ^body : !flag
    }
    // CHECK: repeatable @incomplete = 0
    simulation.func @incomplete(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[5] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      %test = simulation.ref.load %flag : !flag -> i1
      cf.cond_br %test, ^yes, ^wait
    ^yes:
      simulation.ref.store %v to %out : i8, !ref
      cf.br ^wait
    ^wait:
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @repeated = 0
    simulation.func @repeated(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[6] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %out : i8, !ref
      simulation.ref.store %v to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @stateful = 0
    simulation.func @stateful(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[7] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      %old = simulation.ref.load %out : !ref -> i8
      %sum = arith.addi %v, %old : i8
      simulation.ref.store %sum to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @general_always = 0
    simulation.func @general_always(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[8] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      simulation.ref.store %v to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @captured_snapshot = 0
    simulation.func @captured_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[9] : !ref
      %snapshot = simulation.ref.load %input : !ref -> i8
      cf.br ^body
    ^body:
      simulation.ref.store %snapshot to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @conflicting_a = 0
    simulation.func @conflicting_a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[10] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      simulation.ref.store %v to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // CHECK: repeatable @conflicting_b = 0
    simulation.func @conflicting_b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 10 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[10] : !ref
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> i8
      simulation.ref.store %v to %out : i8, !ref
      simulation.suspend.any %input, %flag edges [0, 0] to ^body : !ref, !flag
    }
    // Distinct immutable captures can produce competing values even though
    // the writer inventory refers to the same code owner for both actors.
    // CHECK: repeatable @multiple_instances = 0
    simulation.func @multiple_instances(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 11 : i64} {
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[11] : !ref
      cf.br ^body
    ^body:
      simulation.ref.store %value to %out : i8, !ref
      simulation.suspend.change %flag to ^body : !flag
    }
    // One spawn operation in a loop can still create competing actors.
    // CHECK: repeatable @loop_instance = 0
    simulation.func @loop_instance(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 12 : i64} {
      %flag = simulation.context.storage %ctx[1] : !flag
      %out = simulation.context.storage %ctx[12] : !ref
      cf.br ^body
    ^body:
      simulation.ref.store %value to %out : i8, !ref
      simulation.suspend.change %flag to ^body : !flag
    }
  }
}
