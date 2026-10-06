// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension)))' -o %t
// RUN: FileCheck %s < %t
// RUN: obelisk-opt %t --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension)))' -o %t.twice
// RUN: diff %t %t.twice

// The header uses only induction state. The captured snapshot is live THROUGH
// it into the body. Initial entry and resume meet there, requiring an SSA merge
// even though the header contains no direct use of the snapshot.
module {
  simulation.design @live_through {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always hierarchy "loop"
    // CHECK-LABEL: simulation.func @loop
    // CHECK: %[[SNAPSHOT:.*]] = simulation.ref.load
    // CHECK: cf.br ^[[HEADER:bb[0-9]+]](%{{.*}}, %[[SNAPSHOT]] : i32, i8)
    // CHECK: ^[[RESUME:bb[0-9]+]](%[[RESTORED:.*]]: i8):
    // CHECK: cf.br ^[[HEADER]](%{{.*}}, %[[RESTORED]] : i32, i8)
    // CHECK: ^[[HEADER]](%[[I:.*]]: i32, %[[LIVE:.*]]: i8):
    // CHECK: cf.cond_br %{{.*}}, ^[[BODY:bb[0-9]+]](%[[LIVE]] : i8), ^[[WAIT:bb[0-9]+]](%[[LIVE]] : i8)
    // CHECK: ^[[BODY]](%[[BODY_VALUE:.*]]: i8):
    // CHECK: simulation.ref.store %[[BODY_VALUE]]
    // CHECK: cf.br ^[[HEADER]](%{{.*}}, %[[BODY_VALUE]] : i32, i8)
    // CHECK: ^[[WAIT]](%[[WAIT_VALUE:.*]]: i8):
    // CHECK: simulation.suspend.change %{{.*}} to ^[[RESUME]](%[[WAIT_VALUE]] : i8)
    // CHECK-NOT: simulation.ref.load
    simulation.func @loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      %snapshot = simulation.ref.load %source : !simulation.ref<i8> -> i8
      %zero = arith.constant 0 : i32
      cf.br ^header(%zero : i32)
    ^resume:
      %reset = arith.constant 0 : i32
      cf.br ^header(%reset : i32)
    ^header(%i: i32):
      %limit = arith.constant 2 : i32
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^wait
    ^body:
      simulation.ref.store %snapshot to %out : i8, !simulation.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^header(%next : i32)
    ^wait:
      simulation.suspend.change %source to ^resume : !simulation.ref<i8>
    }
  }
}
