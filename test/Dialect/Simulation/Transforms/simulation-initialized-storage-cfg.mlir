// RUN: obelisk-opt %s --obelisk-sim-propagate-initialized-storage -o %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --obelisk-sim-propagate-initialized-storage -o %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage))' | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage))' | FileCheck %s

// LRM 6.8 requires declaration initialization before all process startups.
// A CFG preheader is unconditional; a skipped initialization edge is not.
module {
  simulation.design @cfg_initialization {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "init"
    simulation.code_unit.decl 3 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 4 in 0 function hierarchy "init_logic"
    simulation.storage.decl 0 in 0 : i32 design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      cf.br ^initialize
    ^initialize:
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
  simulation.design @conditional_initialization {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "init"
    simulation.code_unit.decl 3 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 4 in 0 function hierarchy "init_logic"
    simulation.storage.decl 0 in 0 : i32 design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.storage.decl 2 in 0 : i1 design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %choose_ref = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %choose = simulation.ref.load %choose_ref : !simulation.ref<i1> -> i1
      cf.cond_br %choose, ^initialize, ^startup
    ^initialize:
      simulation.call @init_int(%ctx) : (!simulation.context) -> ()
      simulation.call @init_logic(%ctx) : (!simulation.context) -> ()
      cf.br ^startup
    ^startup:
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
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.load
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
}
