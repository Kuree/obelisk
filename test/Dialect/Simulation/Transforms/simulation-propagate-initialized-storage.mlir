// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage{vpi=read}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage{vpi=full}))' | FileCheck %s --check-prefix=VPI

// Declaration initialization precedes process startup. A whole integer with
// one constant initializer and no later write can be read as that constant.
// Keep the store itself: it remains observable as design state.
module {
  simulation.design @initialized {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "init"
    simulation.code_unit.decl 3 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 4 in 0 function hierarchy "init_logic"
    simulation.storage.decl 0 in 0 : i32 design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      simulation.call @init_int(%ctx) : (!simulation.context) -> ()
      simulation.call @init_logic(%ctx) : (!simulation.context) -> ()
      %int = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %logic = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %process = simulation.spawn @reader(%ctx, %int, %logic) : !simulation.context, !simulation.ref<i32>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      simulation.return
    }

    // CHECK-LABEL: simulation.func @init_int
    // CHECK: simulation.ref.store
    simulation.func @init_int(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 81 : i32
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      simulation.return
    }

    // CHECK-LABEL: simulation.func @init_logic
    // CHECK: simulation.ref.store
    simulation.func @init_logic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %value = simulation.logic.constant 17 : i32, 1 : i32 : !simulation.logic<32>
      %ref = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %value to %ref : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      simulation.return
    }

    // CHECK-LABEL: simulation.func @reader
    // CHECK: arith.constant 81 : i32
    // CHECK: simulation.logic.constant 17 : i32, 1 : i32
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return
    // VPI-LABEL: simulation.func @reader
    // VPI: simulation.ref.load
    // VPI: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %int: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %logic: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %a = simulation.ref.load %int : !simulation.ref<i32> -> i32
      %b = simulation.ref.load %logic : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.return
    }
  }

  // A root-called initializer can read a variable before its declaration
  // store in hand-written IR. No read of that descriptor may be replaced.
  // CHECK-LABEL: simulation.design @read_before_init
  simulation.design @read_before_init {
    simulation.scope.decl 0
    simulation.code_unit.decl 10 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 11 in 0 function hierarchy "early"
    simulation.code_unit.decl 12 in 0 function hierarchy "init"
    simulation.code_unit.decl 13 in 0 initial hierarchy "late"
    simulation.storage.decl 0 in 0 : i32 design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 10 : i64} {
      simulation.call @early(%ctx) : (!simulation.context) -> ()
      simulation.call @init(%ctx) : (!simulation.context) -> ()
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %process = simulation.spawn @late(%ctx, %ref) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func @early(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    simulation.func @init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %value = arith.constant 81 : i32
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      simulation.return
    }
    // CHECK-LABEL: simulation.func @late
    // CHECK: simulation.ref.load
    simulation.func @late(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 13 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
  }

  // Another whole write invalidates the fact, regardless of the value it
  // writes or whether the root started that process after initialization.
  // CHECK-LABEL: simulation.design @later_write
  simulation.design @later_write {
    simulation.scope.decl 0
    simulation.code_unit.decl 20 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 21 in 0 function hierarchy "init"
    simulation.code_unit.decl 22 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 23 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : i32 design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 20 : i64} {
      simulation.call @init(%ctx) : (!simulation.context) -> ()
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %reader = simulation.spawn @reader(%ctx, %ref) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      %writer = simulation.spawn @writer(%ctx, %ref) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func @init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64} {
      %value = arith.constant 81 : i32
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 22 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 23 : i64} {
      %value = arith.constant 82 : i32
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      simulation.return
    }
  }

  // A slice reference is outside the whole-storage proof. Its store must
  // leave even a four-state whole read live.
  // CHECK-LABEL: simulation.design @partial_write
  simulation.design @partial_write {
    simulation.scope.decl 0
    simulation.code_unit.decl 30 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 31 in 0 function hierarchy "init"
    simulation.code_unit.decl 32 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 33 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<32> design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 30 : i64} {
      simulation.call @init(%ctx) : (!simulation.context) -> ()
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<32>>
      %reader = simulation.spawn @reader(%ctx, %ref) : !simulation.context, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %writer = simulation.spawn @writer(%ctx, %ref) : !simulation.context, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      simulation.return
    }
    simulation.func @init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 31 : i64} {
      %value = simulation.logic.constant 17 : i32, 0 : i32 : !simulation.logic<32>
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %value to %ref : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 32 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.return
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 33 : i64} {
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %bit = simulation.ref.extract %ref from 0 : !simulation.ref<!simulation.logic<32>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %bit : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }

  // Hand-written IR may start a reader before the declaration initializer.
  // The root's operation order must establish the initializer first.
  // CHECK-LABEL: simulation.design @spawn_before_init
  simulation.design @spawn_before_init {
    simulation.scope.decl 0
    simulation.code_unit.decl 40 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 41 in 0 function hierarchy "init"
    simulation.code_unit.decl 42 in 0 initial hierarchy "reader"
    simulation.storage.decl 0 in 0 : i32 design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 40 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %reader = simulation.spawn @reader(%ctx, %ref) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      simulation.call @init(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func @init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 41 : i64} {
      %value = arith.constant 81 : i32
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 42 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
  }
}
