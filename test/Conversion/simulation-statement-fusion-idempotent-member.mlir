// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// A retained-state response closes a process-level request/response cycle.
// IEEE 1800-2023 4.3 permits the publication DAG when the state transition is
// idempotent and its intermediate values have no opaque observers. The old
// state and all publications using it must stay together in one source slice.
// Snapshotting overwritten state and advancing a counter are not idempotent.
// CHECK-LABEL: simulation.design @held_member
// CHECK: simulation.func @root
// CHECK-NOT: simulation.spawn
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
// CHECK: simulation.func private @__obelisk_region_kernel_0_0
// CHECK: %[[STATE:.*]] = simulation.context.storage %{{.*}}[5]
// CHECK: %[[OLD:.*]] = simulation.ref.load %[[STATE]]
// CHECK: %[[NEXT:.*]] = arith.select %{{.*}}, %{{.*}}, %[[OLD]]
// CHECK: simulation.ref.store %[[NEXT]] to %[[STATE]]
// CHECK: simulation.ref.store %[[NEXT]] to
// CHECK: simulation.suspend.any
module {
  simulation.design @held_member {
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
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "held"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
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
    simulation.func @held(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
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

// CHECK-LABEL: simulation.design @snapshot_member
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.spawn @producer_and_consumer
// CHECK: simulation.spawn @held
// CHECK-NOT: __obelisk_region_kernel_
module {
  simulation.design @snapshot_member {
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
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "held"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
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
    simulation.func @held(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
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

// CHECK-LABEL: simulation.design @counter_member
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.spawn @producer_and_consumer
// CHECK: simulation.spawn @held
// CHECK-NOT: __obelisk_region_kernel_
module {
  simulation.design @counter_member {
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
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "held"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
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
    simulation.func @held(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      %flag = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %flag, ^update, ^hold
    ^update:
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      cf.br ^publish(%new : i8)
    ^hold:
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      cf.br ^publish(%next : i8)
    ^publish(%value: i8):
      simulation.ref.store %value to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %enable edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>
    }
  }
}
