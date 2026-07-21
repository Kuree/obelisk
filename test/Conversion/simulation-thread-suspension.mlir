// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension)))' | FileCheck %s

module {
  obelisk_sim.design @threading {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @child(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    // Join process operands remain the fixed prefix while newly live values
    // are appended as continuation operands.
    // CHECK-LABEL: obelisk_sim.func @join
    // CHECK: %[[LIVE:.*]] = obelisk_sim.ref.load
    // CHECK: %[[PROCESS:.*]] = obelisk_sim.spawn
    // CHECK: obelisk_sim.suspend.join any %[[PROCESS]], %[[LIVE]] processes 1 to ^[[JOINED:.*]] : !obelisk_sim.process, !obelisk_sim.logic<8>
    // CHECK: ^[[JOINED]](%[[ARG:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.ref.store %[[ARG]]
    obelisk_sim.func @join(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %process = obelisk_sim.spawn @child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.suspend.join any %process processes 1 to ^resume : !obelisk_sim.process
    ^resume:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // A value defined before a suspension and used after it becomes an
    // explicit continuation operand.
    // CHECK-LABEL: obelisk_sim.func @single
    // CHECK: %[[V:.*]] = obelisk_sim.ref.load
    // CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[BB:.*]](%[[V]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[BB]](%[[ARG:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.ref.store %[[ARG]]
    obelisk_sim.func @single(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // A value live across two suspensions has to be forwarded again from the
    // argument the first suspension introduced, not from the original value.
    // CHECK-LABEL: obelisk_sim.func @chained
    // CHECK: %[[V0:.*]] = obelisk_sim.ref.load
    // CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[B1:.*]](%[[V0]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[B1]](%[[A1:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.suspend.change %{{.*}} to ^[[B2:.*]](%[[A1]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[B2]](%[[A2:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.ref.store %[[A2]]
    obelisk_sim.func @chained(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^first
    ^first:
      obelisk_sim.suspend.change %ref to ^second : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    ^second:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // Entry arguments are scheduler-supplied captures and are never threaded.
    // CHECK-LABEL: obelisk_sim.func @captures_are_not_threaded
    // CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[BB:.*]]{{$}}
    // CHECK: ^[[BB]]:
    obelisk_sim.func @captures_are_not_threaded(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // A loop back-edge into the continuation must also carry the value.
    // CHECK-LABEL: obelisk_sim.func @loop
    // CHECK: ^[[HDR:.*]](%[[ARG:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.suspend.change %{{.*}} to ^[[HDR]](%[[ARG]] : !obelisk_sim.logic<8>)
    obelisk_sim.func @loop(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      cf.br ^header
    ^header:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.change %ref to ^header : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }
  }
}
