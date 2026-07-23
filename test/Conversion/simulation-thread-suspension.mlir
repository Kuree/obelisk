// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension)))' | FileCheck %s

module {
  obelisk_sim.design @threading {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.threading.child.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.threading.join.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.threading.single.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.threading.chained.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 initial hierarchy "test.threading.captures_are_not_threaded.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 always hierarchy "test.threading.loop.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 initial hierarchy "test.threading.constants.9000007"
    obelisk_sim.code_unit.decl 9000008 in 0 initial hierarchy "test.threading.merge.9000008"
    obelisk_sim.code_unit.decl 9000009 in 0 initial hierarchy "test.threading.duplicate_edges.9000009"
    obelisk_sim.code_unit.decl 9000010 in 0 observer hierarchy "test.threading.observer.9000010"
    obelisk_sim.code_unit.decl 9000011 in 0 initial hierarchy "test.threading.observer_lifetime.9000011"
    obelisk_sim.code_unit.decl 9000012 in 0 initial hierarchy "test.threading.observer_nondominating_capture.9000012"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func private @observer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000010 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %truth = obelisk_sim.logic.is_true %value : !obelisk_sim.logic<1>
      obelisk_sim.return %truth : i1
    }

    // Automatic observer captures stay private to the suspended edge. A
    // bridge consumes the retained capture before entering a continuation
    // that is also reachable without suspending.
    // CHECK-LABEL: obelisk_sim.func @observer_lifetime
    // CHECK: %[[LOCAL:.*]] = obelisk_sim.ref.alloc
    // CHECK: %[[BOUND:.*]] = obelisk_sim.observer.bind @observer
    // CHECK: cf.cond_br %{{.*}}, ^[[RESUME:.*]], ^[[WAIT:.*]]
    // CHECK: ^[[WAIT]]:
    // CHECK: obelisk_sim.suspend.observe %[[BOUND]], %{{.*}}, %[[LOCAL]] conditions 0 edges [0] indices [-1] to ^[[BRIDGE:bb[0-9]+]]
    // CHECK: ^[[BRIDGE]](%{{.*}}: !obelisk_sim.ref<!obelisk_sim.logic<1>>):
    // CHECK-NEXT: cf.br ^[[RESUME]]
    // CHECK: ^[[RESUME]]:
    obelisk_sim.func @observer_lifetime(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000011 : i64} {
      %initial = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %local = obelisk_sim.ref.alloc %initial : !obelisk_sim.logic<1> -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %bound = obelisk_sim.observer.bind @observer values(%local, %local : !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) captures 1 : !obelisk_sim.observer<i1>
      %false = arith.constant false
      cf.cond_br %false, ^resume, ^wait
    ^wait:
      obelisk_sim.suspend.observe %bound, %false conditions 0 edges [0] indices [-1] to ^resume : !obelisk_sim.observer<i1>, i1
    ^resume:
      obelisk_sim.return
    }

    // A capture defined only on the suspended predecessor must never be added
    // to another predecessor of the shared continuation. This is the
    // non-dominating SSA shape that motivated the private bridge.
    // CHECK-LABEL: obelisk_sim.func @observer_nondominating_capture
    // CHECK: cf.cond_br %{{.*}}, ^[[WAIT:.*]], ^[[DIRECT:.*]]
    // CHECK: ^[[WAIT]]:
    // CHECK: %[[LOCAL:.*]] = obelisk_sim.ref.alloc
    // CHECK: %[[BOUND:.*]] = obelisk_sim.observer.bind @observer
    // CHECK: obelisk_sim.suspend.observe %[[BOUND]], %{{.*}}, %[[LOCAL]] conditions 0 edges [0] indices [-1] to ^[[BRIDGE:bb[0-9]+]]
    // CHECK: ^[[DIRECT]]:
    // CHECK-NEXT: cf.br ^[[RESUME:.*]]
    // CHECK: ^[[BRIDGE]](%{{.*}}: !obelisk_sim.ref<!obelisk_sim.logic<1>>):
    // CHECK-NEXT: cf.br ^[[RESUME]]
    // CHECK: ^[[RESUME]]:
    obelisk_sim.func @observer_nondominating_capture(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000012 : i64} {
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^direct
    ^wait:
      %initial = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %local = obelisk_sim.ref.alloc %initial : !obelisk_sim.logic<1> -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %bound = obelisk_sim.observer.bind @observer values(%local, %local : !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) captures 1 : !obelisk_sim.observer<i1>
      %false = arith.constant false
      obelisk_sim.suspend.observe %bound, %false conditions 0 edges [0] indices [-1] to ^resume : !obelisk_sim.observer<i1>, i1
    ^direct:
      cf.br ^resume
    ^resume:
      obelisk_sim.return
    }

    obelisk_sim.func @child(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
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
    obelisk_sim.func @join(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
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
    obelisk_sim.func @single(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
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
    obelisk_sim.func @chained(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
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
    obelisk_sim.func @captures_are_not_threaded(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
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
    obelisk_sim.func @loop(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9000006 : i64} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      cf.br ^header
    ^header:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.change %ref to ^header : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }

    // Constants are rematerialized in the continuation instead of consuming
    // frame slots. This is required for byte strings, which deliberately have
    // no pointer-bearing frame representation, and keeps scalar constants
    // available on every resumed loop activation.
    // CHECK-LABEL: obelisk_sim.func @constants
    // CHECK: %[[MESSAGE:.*]] = obelisk_sim.bytes.constant "resumed"
    // CHECK: %[[FD:.*]] = arith.constant 1 : i32
    // CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[RESUME:.*]]{{$}}
    // CHECK: ^[[RESUME]]:
    // CHECK: %[[REMATERIALIZED:.*]] = obelisk_sim.bytes.constant "resumed"
    // CHECK: %[[REMATERIALIZED_FD:.*]] = arith.constant {{.*}}1 : i32
    // CHECK: obelisk_sim.display {{.*}} to %[[REMATERIALIZED_FD]](%[[REMATERIALIZED]])
    obelisk_sim.func @constants(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %message = obelisk_sim.bytes.constant "resumed"
      %fd = arith.constant 1 : i32
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      obelisk_sim.display %ctx to %fd(%message) newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    // A value restored on one path must continue through a downstream merge.
    // The unsuspended predecessor supplies the original value, while the
    // resumed predecessor supplies the continuation argument.
    // CHECK-LABEL: obelisk_sim.func @merge
    // CHECK: %[[LIVE:.*]] = obelisk_sim.ref.load
    // CHECK: cf.cond_br %{{.*}}, ^[[SUSPEND:bb[0-9]+]], ^[[DIRECT:bb[0-9]+]]
    // CHECK: ^[[SUSPEND]]:
    // CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[RESUME:bb[0-9]+]](%[[LIVE]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[RESUME]](%[[RESTORED:.*]]: !obelisk_sim.logic<8>):
    // CHECK: cf.br ^[[MERGE:bb[0-9]+]](%[[RESTORED]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[DIRECT]]:
    // CHECK: cf.br ^[[MERGE]](%[[LIVE]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[MERGE]](%[[MERGED:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.ref.store %[[MERGED]]
    obelisk_sim.func @merge(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000008 : i64} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %condition = arith.constant true
      cf.cond_br %condition, ^suspend, ^direct
    ^suspend:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      cf.br ^merge
    ^direct:
      cf.br ^merge
    ^merge:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // A predecessor can have two CFG edges to the same merge. Each edge gets
    // exactly one threaded operand even though getPredecessors() reports both.
    // CHECK-LABEL: obelisk_sim.func @duplicate_edges
    // CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[DISPATCH:bb[0-9]+]](%[[LIVE:.*]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[DISPATCH]](%[[RESTORED:.*]]: !obelisk_sim.logic<8>):
    // CHECK: cf.cond_br %{{.*}}, ^[[MERGE:bb[0-9]+]](%[[RESTORED]] : !obelisk_sim.logic<8>), ^[[MERGE]](%[[RESTORED]] : !obelisk_sim.logic<8>)
    // CHECK: ^[[MERGE]](%[[ARG:.*]]: !obelisk_sim.logic<8>):
    // CHECK: obelisk_sim.ref.store %[[ARG]]
    obelisk_sim.func @duplicate_edges(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %live = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^dispatch
    ^dispatch:
      %condition = arith.constant true
      cf.cond_br %condition, ^merge, ^merge
    ^merge:
      obelisk_sim.ref.store %live to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
  }
}
