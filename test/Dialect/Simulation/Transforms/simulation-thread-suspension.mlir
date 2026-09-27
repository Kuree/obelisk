// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension)))' | FileCheck %s

module {
  simulation.design @threading {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.threading.child.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.threading.join.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.threading.single.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.threading.chained.9000004"
    simulation.code_unit.decl 9000005 in 0 initial hierarchy "test.threading.captures_are_not_threaded.9000005"
    simulation.code_unit.decl 9000006 in 0 always hierarchy "test.threading.loop.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.threading.constants.9000007"
    simulation.code_unit.decl 9000008 in 0 initial hierarchy "test.threading.merge.9000008"
    simulation.code_unit.decl 9000009 in 0 initial hierarchy "test.threading.duplicate_edges.9000009"
    simulation.code_unit.decl 9000010 in 0 observer hierarchy "test.threading.observer.9000010"
    simulation.code_unit.decl 9000011 in 0 initial hierarchy "test.threading.observer_lifetime.9000011"
    simulation.code_unit.decl 9000012 in 0 initial hierarchy "test.threading.observer_nondominating_capture.9000012"
    simulation.code_unit.decl 9000013 in 0 always hierarchy "test.threading.loop_constant_expression.9000013"
    simulation.code_unit.decl 9000014 in 0 always hierarchy "test.threading.self_loop_next.9000014"
    simulation.code_unit.decl 9000015 in 0 always hierarchy "test.threading.self_loop_side_next.9000015"
    simulation.code_unit.decl 9000016 in 0 initial hierarchy "test.threading.control_body_restored.9000016"
    simulation.code_unit.decl 9000017 in 0 initial hierarchy "test.threading.observer_state.9000017"
    simulation.code_unit.decl 9000018 in 0 always hierarchy "test.threading.loop_sampled_expression.9000018"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design

    simulation.func private @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000010 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %truth = simulation.logic.is_true %value : !simulation.logic<1>
      simulation.return %truth : i1
    }

    // Observer descriptors are planning-only clock operands only in Clause 31
    // timing-check coordinators. An ordinary process may keep one live across
    // a suspension and must receive it in its canonical continuation frame
    // like any other SSA value.
    // CHECK-LABEL: simulation.func @observer_state
    // CHECK: %[[LIVE_OBSERVER:.*]] = simulation.observer.bind @observer
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[OBSERVER_RESUME:.*]](%[[LIVE_OBSERVER]] : !simulation.observer<i1>)
    // CHECK: ^[[OBSERVER_RESUME]](%[[RESTORED_OBSERVER:.*]]: !simulation.observer<i1>):
    // CHECK: simulation.suspend.observe %[[RESTORED_OBSERVER]]
    simulation.func @observer_state(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000017 : i64} {
      %bound = simulation.observer.bind @observer
          values(%source, %source : !simulation.ref<!simulation.logic<1>>,
                 !simulation.ref<!simulation.logic<1>>) captures 1 :
          !simulation.observer<i1>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %false = arith.constant false
      simulation.suspend.observe %bound, %false conditions 0 edges [0]
          indices [-1] to ^done : !simulation.observer<i1>, i1
    ^done:
      simulation.return
    }

    // Automatic observer captures stay private to the suspended edge. A
    // bridge consumes the retained capture before entering a continuation
    // that is also reachable without suspending.
    // CHECK-LABEL: simulation.func @observer_lifetime
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK: %[[BOUND:.*]] = simulation.observer.bind @observer
    // CHECK: cf.cond_br %{{.*}}, ^[[RESUME:.*]], ^[[WAIT:.*]]
    // CHECK: ^[[WAIT]]:
    // CHECK: simulation.suspend.observe %[[BOUND]], %{{.*}}, %[[LOCAL]] conditions 0 edges [0] indices [-1] to ^[[BRIDGE:bb[0-9]+]]
    // CHECK: ^[[BRIDGE]](%{{.*}}: !simulation.ref<!simulation.logic<1>>):
    // CHECK-NEXT: cf.br ^[[RESUME]]
    // CHECK: ^[[RESUME]]:
    simulation.func @observer_lifetime(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000011 : i64} {
      %initial = simulation.logic.constant false, false : !simulation.logic<1>
      %local = simulation.ref.alloc %initial : !simulation.logic<1> -> !simulation.ref<!simulation.logic<1>>
      %bound = simulation.observer.bind @observer values(%local, %local : !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>) captures 1 : !simulation.observer<i1>
      %false = arith.constant false
      cf.cond_br %false, ^resume, ^wait
    ^wait:
      simulation.suspend.observe %bound, %false conditions 0 edges [0] indices [-1] to ^resume : !simulation.observer<i1>, i1
    ^resume:
      simulation.return
    }

    // A capture defined only on the suspended predecessor must never be added
    // to another predecessor of the shared continuation. This is the
    // non-dominating SSA shape that motivated the private bridge.
    // CHECK-LABEL: simulation.func @observer_nondominating_capture
    // CHECK: cf.cond_br %{{.*}}, ^[[WAIT:.*]], ^[[DIRECT:.*]]
    // CHECK: ^[[WAIT]]:
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK: %[[BOUND:.*]] = simulation.observer.bind @observer
    // CHECK: simulation.suspend.observe %[[BOUND]], %{{.*}}, %[[LOCAL]] conditions 0 edges [0] indices [-1] to ^[[BRIDGE:bb[0-9]+]]
    // CHECK: ^[[DIRECT]]:
    // CHECK-NEXT: cf.br ^[[RESUME:.*]]
    // CHECK: ^[[BRIDGE]](%{{.*}}: !simulation.ref<!simulation.logic<1>>):
    // CHECK-NEXT: cf.br ^[[RESUME]]
    // CHECK: ^[[RESUME]]:
    simulation.func @observer_nondominating_capture(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000012 : i64} {
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^direct
    ^wait:
      %initial = simulation.logic.constant false, false : !simulation.logic<1>
      %local = simulation.ref.alloc %initial : !simulation.logic<1> -> !simulation.ref<!simulation.logic<1>>
      %bound = simulation.observer.bind @observer values(%local, %local : !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>) captures 1 : !simulation.observer<i1>
      %false = arith.constant false
      simulation.suspend.observe %bound, %false conditions 0 edges [0] indices [-1] to ^resume : !simulation.observer<i1>, i1
    ^direct:
      cf.br ^resume
    ^resume:
      simulation.return
    }

    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }

    // Join process operands remain the fixed prefix while newly live values
    // are appended as continuation operands.
    // CHECK-LABEL: simulation.func @join
    // CHECK: %[[LIVE:.*]] = simulation.ref.load
    // CHECK: %[[PROCESS:.*]] = simulation.spawn
    // CHECK: simulation.suspend.join any %[[PROCESS]], %[[LIVE]] processes 1 to ^[[JOINED:.*]] : !simulation.process, !simulation.logic<8>
    // CHECK: ^[[JOINED]](%[[ARG:.*]]: !simulation.logic<8>):
    // CHECK: simulation.ref.store %[[ARG]]
    simulation.func @join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %process = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
      simulation.suspend.join any %process processes 1 to ^resume : !simulation.process
    ^resume:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // A value defined before a suspension and used after it becomes an
    // explicit continuation operand.
    // CHECK-LABEL: simulation.func @single
    // CHECK: %[[V:.*]] = simulation.ref.load
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[BB:.*]](%[[V]] : !simulation.logic<8>)
    // CHECK: ^[[BB]](%[[ARG:.*]]: !simulation.logic<8>):
    // CHECK: simulation.ref.store %[[ARG]]
    simulation.func @single(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^resume
    ^resume:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // A value live across two suspensions has to be forwarded again from the
    // argument the first suspension introduced, not from the original value.
    // CHECK-LABEL: simulation.func @chained
    // CHECK: %[[V0:.*]] = simulation.ref.load
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[B1:.*]](%[[V0]] : !simulation.logic<8>)
    // CHECK: ^[[B1]](%[[A1:.*]]: !simulation.logic<8>):
    // CHECK: simulation.suspend.change %{{.*}} to ^[[B2:.*]](%[[A1]] : !simulation.logic<8>)
    // CHECK: ^[[B2]](%[[A2:.*]]: !simulation.logic<8>):
    // CHECK: simulation.ref.store %[[A2]]
    simulation.func @chained(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^first
    ^first:
      simulation.suspend.change %ref to ^second : !simulation.ref<!simulation.logic<8>>
    ^second:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // Entry arguments are scheduler-supplied captures and are never threaded.
    // CHECK-LABEL: simulation.func @captures_are_not_threaded
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[BB:.*]]{{$}}
    // CHECK: ^[[BB]]:
    simulation.func @captures_are_not_threaded(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %value = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // A value restored before a named-block boundary is directly available
    // to its synchronous body. The body edge cannot carry block arguments;
    // only the boundary's resumable exit has continuation operands.
    // CHECK-LABEL: simulation.func @control_body_restored
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[CONTROL_START:.*]](%[[CONTROL_LIVE:.*]] : !simulation.logic<8>)
    // CHECK: ^[[CONTROL_START]](%[[CONTROL_RESTORED:.*]]: !simulation.logic<8>):
    // CHECK: simulation.control.boundary %{{.*}} resume ^[[CONTROL_EXIT:.*]] body ^[[CONTROL_BODY:.*]]
    // CHECK: ^[[CONTROL_EXIT]]:
    // CHECK: ^[[CONTROL_BODY]]:
    // CHECK: simulation.ref.store %[[CONTROL_RESTORED]]
    simulation.func @control_body_restored(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000016 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^start
    ^start:
      %activation = simulation.control.enter 1
      simulation.control.boundary %activation resume ^exit body ^body
    ^exit:
      simulation.return
    ^body:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.control.leave %activation
      cf.br ^exit
    }

    // A loop back-edge into the continuation must also carry the value.
    // CHECK-LABEL: simulation.func @loop
    // CHECK: ^[[HDR:.*]](%[[ARG:.*]]: !simulation.logic<8>):
    // CHECK: simulation.suspend.change %{{.*}} to ^[[HDR]](%[[ARG]] : !simulation.logic<8>)
    simulation.func @loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9000006 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      cf.br ^header
    ^header:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %ref to ^header : !simulation.ref<!simulation.logic<8>>
    }

    // Explicit self-loop state distinguishes the previous iteration's block
    // argument from a fresh value computed in the current iteration. The
    // header dominates the side block, but threading must not rewrite that
    // side block's current-value consumer back to the previous value.
    // CHECK-LABEL: simulation.func @self_loop_next
    // CHECK: ^[[SELF_HEADER:.*]](%[[PREVIOUS:.*]]: !simulation.logic<8>):
    // CHECK: %[[NEXT:.*]] = simulation.ref.load
    // CHECK: cf.br ^[[SELF_SIDE:.*]]
    // CHECK: ^[[SELF_SIDE]]:
    // CHECK: simulation.logic.binary xor %[[NEXT]], %[[NEXT]]
    // CHECK: simulation.suspend.change %{{.*}} to ^[[SELF_HEADER]](%[[NEXT]] : !simulation.logic<8>)
    simulation.func @self_loop_next(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9000014 : i64} {
      %initial = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      cf.br ^header(%initial : !simulation.logic<8>)
    ^header(%previous: !simulation.logic<8>):
      %next = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      cf.br ^side
    ^side:
      %current = simulation.logic.binary xor %next, %next : !simulation.logic<8>
      simulation.ref.store %current to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %ref to ^header(%next : !simulation.logic<8>) : !simulation.ref<!simulation.logic<8>>
    }

    // The freshly forwarded value can also be defined in a side block inside
    // the loop. It remains distinct from the header's previous-state argument.
    // CHECK-LABEL: simulation.func @self_loop_side_next
    // CHECK: ^[[SIDE_HEADER:.*]](%[[SIDE_PREVIOUS:.*]]: !simulation.logic<8>):
    // CHECK: cf.br ^[[SIDE_BODY:.*]]
    // CHECK: ^[[SIDE_BODY]]:
    // CHECK: %[[SIDE_NEXT:.*]] = simulation.ref.load
    // CHECK: simulation.logic.binary xor %[[SIDE_NEXT]], %[[SIDE_NEXT]]
    // CHECK: simulation.suspend.change %{{.*}} to ^[[SIDE_HEADER]](%[[SIDE_NEXT]] : !simulation.logic<8>)
    simulation.func @self_loop_side_next(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9000015 : i64} {
      %initial = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      cf.br ^header(%initial : !simulation.logic<8>)
    ^header(%previous: !simulation.logic<8>):
      cf.br ^side
    ^side:
      %next = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %current = simulation.logic.binary xor %next, %next : !simulation.logic<8>
      simulation.ref.store %current to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %ref to ^header(%next : !simulation.logic<8>) : !simulation.ref<!simulation.logic<8>>
    }

    // Constants are rematerialized in the continuation instead of consuming
    // frame slots. This is required for byte strings, which deliberately have
    // no pointer-bearing frame representation, and keeps scalar constants
    // available on every resumed loop activation.
    // CHECK-LABEL: simulation.func @constants
    // CHECK: %[[MESSAGE:.*]] = simulation.bytes.constant "resumed"
    // CHECK: %[[FD:.*]] = arith.constant 1 : i32
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[RESUME:.*]]{{$}}
    // CHECK: ^[[RESUME]]:
    // CHECK: %[[REMATERIALIZED:.*]] = simulation.bytes.constant "resumed"
    // CHECK: %[[REMATERIALIZED_FD:.*]] = arith.constant {{.*}}1 : i32
    // CHECK: simulation.display {{.*}} to %[[REMATERIALIZED_FD]](%[[REMATERIALIZED]])
    simulation.func @constants(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %message = simulation.bytes.constant "resumed"
      %fd = arith.constant 1 : i32
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      simulation.display %ctx to %fd(%message) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    // A packed cast of a literal is rematerialized inside the activation.
    // It must not create loop-carried frame state and disable Tier 1.
    // CHECK-LABEL: simulation.func @loop_constant_expression
    // CHECK: cf.br ^[[WAIT:bb[0-9]+]]{{$}}
    // CHECK: ^[[WAIT]]:
    // CHECK: simulation.suspend.change %{{.*}} to ^[[BODY:bb[0-9]+]] :
    // CHECK: ^[[BODY]]:
    // CHECK: %[[BITS:.*]] = simulation.logic.constant 1 : i8, 0 : i8
    // CHECK: %[[DERIVED:.*]] = simulation.packed.unflatten %[[BITS]]
    // CHECK: simulation.ref.store %[[DERIVED]]
    // CHECK: cf.br ^[[WAIT]]{{$}}
    simulation.func @loop_constant_expression(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9000013 : i64} {
      %bits = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %derived = simulation.packed.unflatten %bits : (!simulation.logic<8>) -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^body : !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
    ^body:
      simulation.ref.store %derived to %ref : !simulation.packed_array<7 : 0 x !simulation.logic<1>>, !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
      cf.br ^wait
    }

    // In contrast, a cast of a pre-event load is a snapshot, not a constant.
    // Retain the original loop-carried regression for this non-rematerializable
    // case: replaying the load after the event would observe the wrong value.
    // CHECK-LABEL: simulation.func @loop_sampled_expression
    // CHECK: %[[SAMPLED:.*]] = simulation.packed.unflatten
    // CHECK: cf.br ^[[WAIT:bb[0-9]+]](%[[SAMPLED]] :
    // CHECK: ^[[WAIT]](%[[CURRENT:[a-zA-Z0-9_]+]]:
    // CHECK: simulation.suspend.change %{{.*}} to ^[[BODY:bb[0-9]+]](%[[CURRENT]] :
    // CHECK: ^[[BODY]](%[[RESTORED:[a-zA-Z0-9_]+]]:
    // CHECK: simulation.ref.store %[[RESTORED]]
    // CHECK: cf.br ^[[WAIT]](%[[RESTORED]] :
    simulation.func @loop_sampled_expression(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %bits_ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %ref: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000018 : i64} {
      %bits = simulation.ref.load %bits_ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %derived = simulation.packed.unflatten %bits : (!simulation.logic<8>) -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^body : !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
    ^body:
      simulation.ref.store %derived to %ref : !simulation.packed_array<7 : 0 x !simulation.logic<1>>, !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
      cf.br ^wait
    }

    // A value restored on one path must continue through a downstream merge.
    // The unsuspended predecessor supplies the original value, while the
    // resumed predecessor supplies the continuation argument.
    // CHECK-LABEL: simulation.func @merge
    // CHECK: %[[LIVE:.*]] = simulation.ref.load
    // CHECK: cf.cond_br %{{.*}}, ^[[SUSPEND:bb[0-9]+]], ^[[DIRECT:bb[0-9]+]]
    // CHECK: ^[[SUSPEND]]:
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[RESUME:bb[0-9]+]](%[[LIVE]] : !simulation.logic<8>)
    // CHECK: ^[[RESUME]](%[[RESTORED:.*]]: !simulation.logic<8>):
    // CHECK: cf.br ^[[MERGE:bb[0-9]+]](%[[RESTORED]] : !simulation.logic<8>)
    // CHECK: ^[[DIRECT]]:
    // CHECK: cf.br ^[[MERGE]](%[[LIVE]] : !simulation.logic<8>)
    // CHECK: ^[[MERGE]](%[[MERGED:.*]]: !simulation.logic<8>):
    // CHECK: simulation.ref.store %[[MERGED]]
    simulation.func @merge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000008 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %condition = arith.constant true
      cf.cond_br %condition, ^suspend, ^direct
    ^suspend:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      cf.br ^merge
    ^direct:
      cf.br ^merge
    ^merge:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // A predecessor can have two CFG edges to the same merge. Each edge gets
    // exactly one threaded operand even though getPredecessors() reports both.
    // CHECK-LABEL: simulation.func @duplicate_edges
    // CHECK: simulation.suspend.delay %{{.*}} to ^[[DISPATCH:bb[0-9]+]](%[[LIVE:.*]] : !simulation.logic<8>)
    // CHECK: ^[[DISPATCH]](%[[RESTORED:.*]]: !simulation.logic<8>):
    // CHECK: cf.cond_br %{{.*}}, ^[[MERGE:bb[0-9]+]](%[[RESTORED]] : !simulation.logic<8>), ^[[MERGE]](%[[RESTORED]] : !simulation.logic<8>)
    // CHECK: ^[[MERGE]](%[[ARG:.*]]: !simulation.logic<8>):
    // CHECK: simulation.ref.store %[[ARG]]
    simulation.func @duplicate_edges(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %live = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^dispatch
    ^dispatch:
      %condition = arith.constant true
      cf.cond_br %condition, ^merge, ^merge
    ^merge:
      simulation.ref.store %live to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
  }
}
