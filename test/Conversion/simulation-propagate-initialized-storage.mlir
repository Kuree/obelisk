// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-propagate-initialized-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-propagate-initialized-storage{vpi=read}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-propagate-initialized-storage{vpi=full}))' | FileCheck %s --check-prefix=VPI

// Declaration initialization precedes process startup. A whole integer with
// one constant initializer and no later write can be read as that constant.
// Keep the store itself: it remains observable as design state.
module {
  obelisk_sim.design @initialized {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "init"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "init_logic"
    obelisk_sim.storage.decl 0 in 0 : i32 design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.call @init_int(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @init_logic(%ctx) : (!obelisk_sim.context) -> ()
      %int = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %logic = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %process = obelisk_sim.spawn @reader(%ctx, %int, %logic) : !obelisk_sim.context, !obelisk_sim.ref<i32>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @init_int
    // CHECK: obelisk_sim.ref.store
    obelisk_sim.func @init_int(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 81 : i32
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @init_logic
    // CHECK: obelisk_sim.ref.store
    obelisk_sim.func @init_logic(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %value = obelisk_sim.logic.constant 17 : i32, 1 : i32 : !obelisk_sim.logic<32>
      %ref = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: arith.constant 81 : i32
    // CHECK: obelisk_sim.logic.constant 17 : i32, 1 : i32
    // CHECK-NOT: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    // VPI-LABEL: obelisk_sim.func @reader
    // VPI: obelisk_sim.ref.load
    // VPI: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %int: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %logic: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %a = obelisk_sim.ref.load %int : !obelisk_sim.ref<i32> -> i32
      %b = obelisk_sim.ref.load %logic : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.return
    }
  }

  // A root-called initializer can read a variable before its declaration
  // store in hand-written IR. No read of that descriptor may be replaced.
  // CHECK-LABEL: obelisk_sim.design @read_before_init
  obelisk_sim.design @read_before_init {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 10 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "early"
    obelisk_sim.code_unit.decl 12 in 0 function hierarchy "init"
    obelisk_sim.code_unit.decl 13 in 0 initial hierarchy "late"
    obelisk_sim.storage.decl 0 in 0 : i32 design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 10 : i64} {
      obelisk_sim.call @early(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @init(%ctx) : (!obelisk_sim.context) -> ()
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %process = obelisk_sim.spawn @late(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @early(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    obelisk_sim.func @init(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %value = arith.constant 81 : i32
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @late
    // CHECK: obelisk_sim.ref.load
    obelisk_sim.func @late(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 13 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
  }

  // Another whole write invalidates the fact, regardless of the value it
  // writes or whether the root started that process after initialization.
  // CHECK-LABEL: obelisk_sim.design @later_write
  obelisk_sim.design @later_write {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 20 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 21 in 0 function hierarchy "init"
    obelisk_sim.code_unit.decl 22 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 23 in 0 initial hierarchy "writer"
    obelisk_sim.storage.decl 0 in 0 : i32 design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 20 : i64} {
      obelisk_sim.call @init(%ctx) : (!obelisk_sim.context) -> ()
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %reader = obelisk_sim.spawn @reader(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      %writer = obelisk_sim.spawn @writer(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @init(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64} {
      %value = arith.constant 81 : i32
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 22 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    obelisk_sim.func @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 23 : i64} {
      %value = arith.constant 82 : i32
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
  }

  // A slice reference is outside the whole-storage proof. Its store must
  // leave even a four-state whole read live.
  // CHECK-LABEL: obelisk_sim.design @partial_write
  obelisk_sim.design @partial_write {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 30 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 31 in 0 function hierarchy "init"
    obelisk_sim.code_unit.decl 32 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 33 in 0 initial hierarchy "writer"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<32> design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 30 : i64} {
      obelisk_sim.call @init(%ctx) : (!obelisk_sim.context) -> ()
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %reader = obelisk_sim.spawn @reader(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %writer = obelisk_sim.spawn @writer(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @init(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 31 : i64} {
      %value = obelisk_sim.logic.constant 17 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 32 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.return
    }
    obelisk_sim.func @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 33 : i64} {
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %bit = obelisk_sim.ref.extract %ref from 0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero to %bit : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }

  // Hand-written IR may start a reader before the declaration initializer.
  // The root's operation order must establish the initializer first.
  // CHECK-LABEL: obelisk_sim.design @spawn_before_init
  obelisk_sim.design @spawn_before_init {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 40 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 41 in 0 function hierarchy "init"
    obelisk_sim.code_unit.decl 42 in 0 initial hierarchy "reader"
    obelisk_sim.storage.decl 0 in 0 : i32 design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 40 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %reader = obelisk_sim.spawn @reader(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      obelisk_sim.call @init(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func @init(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 41 : i64} {
      %value = arith.constant 81 : i32
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 42 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
  }
}
