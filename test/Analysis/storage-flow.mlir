// RUN: obelisk-opt %s --test-obelisk-storage-flow -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --test-obelisk-storage-flow -o /dev/null 2>&1 | FileCheck %s
// LRM 6.8, 6.21, 9.4, 10.4.2: must definitions, activation boundaries and
// one execution over the entire process lifetime are distinct certificates.
module {
  simulation.design @flow {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 static
    simulation.code_unit.decl 1 in 0 function hierarchy "dominating"
    simulation.code_unit.decl 2 in 0 function hierarchy "conditional"
    simulation.code_unit.decl 3 in 0 initial hierarchy "zero_delay"
    simulation.code_unit.decl 4 in 0 always hierarchy "repeated"

    // CHECK-LABEL: function dominating
    // CHECK: definition 0 lifetime-once=1
    // CHECK: read 0 prefix=1 must=0,
    simulation.func @dominating(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %one = arith.constant 1 : i32
      simulation.ref.store %one to %ref : i32, !simulation.ref<i32>
      cf.cond_br %choose, ^left, ^right
    ^left:
      cf.br ^join
    ^right:
      cf.br ^join
    ^join:
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // CHECK-LABEL: function conditional
    // CHECK: definition 0 lifetime-once=1
    // CHECK: read 0 prefix=1 must={{$}}
    simulation.func @conditional(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %one = arith.constant 1 : i32
      cf.cond_br %choose, ^store, ^join
    ^store:
      simulation.ref.store %one to %ref : i32, !simulation.ref<i32>
      cf.br ^join
    ^join:
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // #0 ends an activation even though it does not start a fresh NBA window.
    // CHECK-LABEL: function zero_delay
    // CHECK: definition 0 lifetime-once=1
    // CHECK: read 0 prefix=0 must={{$}}
    simulation.func @zero_delay(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %one = arith.constant 1 : i32
      simulation.ref.store %one to %ref : i32, !simulation.ref<i32>
      %zero = simulation.time.constant 0
      simulation.suspend.delay %zero to ^resume
    ^resume:
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // CHECK-LABEL: function repeated
    // CHECK: definition 0 lifetime-once=0
    // CHECK: read 0 prefix=0 must=0,
    simulation.func @repeated(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 4 : i64, entry_kind = 3 : i32} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %delay = simulation.time.constant 1
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %delay to ^body
    ^body:
      simulation.ref.store %one to %ref : i32, !simulation.ref<i32>
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      cf.br ^wait
    }
  }
}
