// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// IEEE 1800-2023 4.3: an exclusively owned conditional hold may remain an
// outside observer when a path-sensitive proof establishes idempotence under
// unchanged inputs. Keep its original state and activation protocol. Old-state
// snapshots and downstream transition counters still block the transformation.
// CHECK-LABEL: simulation.design @held_observer
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK: simulation.spawn @held
// CHECK: simulation.func @held
// CHECK: simulation.ref.load
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.suspend.any
module {
  simulation.design @held_observer {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "held"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @held(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    simulation.func @held(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      %flag = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %flag, ^update, ^hold
    ^update:
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      cf.br ^publish(%new : i8)
    ^hold:
      cf.br ^publish(%old : i8)
    ^publish(%value: i8):
      simulation.ref.store %value to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %enable edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @snapshot_observer
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.spawn @producer_and_consumer
// CHECK: simulation.spawn @transform
// CHECK: simulation.spawn @held
// CHECK-NOT: __obelisk_region_kernel_
module {
  simulation.design @snapshot_observer {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "held"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @held(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    simulation.func @held(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      %flag = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %flag, ^update, ^hold
    ^update:
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      cf.br ^publish(%new : i8)
    ^hold:
      cf.br ^publish(%old : i8)
    ^publish(%value: i8):
      simulation.ref.store %value to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %old to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %enable edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @counted_observer
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.spawn @producer_and_consumer
// CHECK: simulation.spawn @transform
// CHECK: simulation.spawn @held
// CHECK-NOT: __obelisk_region_kernel_
module {
  simulation.design @counted_observer {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "held"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @held(%ctx) : !simulation.context -> !simulation.process
      %d = simulation.spawn @sample(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    simulation.func @held(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      %flag = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %flag, ^update, ^hold
    ^update:
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      cf.br ^publish(%new : i8)
    ^hold:
      cf.br ^publish(%old : i8)
    ^publish(%value: i8):
      simulation.ref.store %value to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %enable edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %count = simulation.context.storage %ctx[7] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %output to ^body : !simulation.ref<i8>
    ^body:
      %old = simulation.ref.load %count : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.ref.store %next to %count : i8, !simulation.ref<i8>
      cf.br ^wait
    }
  }
}
