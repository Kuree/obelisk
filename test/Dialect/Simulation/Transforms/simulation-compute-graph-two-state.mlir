// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // A fragment is two-state only when no four-state value it produces or
  // consumes can hold X or Z. The suspension terminator is the one exemption:
  // it forwards the frame rather than computing with it, and the resuming
  // fragment proves those values itself. That exemption belongs to the
  // terminator alone.
  simulation.design @two_state {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.two_state.forwards_only.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.two_state.also_consumes.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.two_state.known_callee.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.two_state.interproc_caller.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.two_state.unknown_callee.9000005"
    simulation.code_unit.decl 9000006 in 0 initial hierarchy "test.two_state.interproc_unknown_caller.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.two_state.spawned_known.9000007"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    // Graph nodes are ordered by function symbol, so @also_consumes is first.
    // Its middle fragment both stores and forwards the same value. The root is
    // inductively two-state: when it is known, every writer preserves that
    // property. This is kernel eligibility, not a claim that startup is
    // already known; a four-state variant remains available until promotion.
    // CHECK: function = @also_consumes, block = 1
    // CHECK-SAME: twoState = true

    // The same inductive-root assumption flows through loads and block
    // arguments, making the whole forwarding chain eligible for a promoted
    // two-state kernel.
    // CHECK: function = @forwards_only, block = 0
    // CHECK-SAME: twoState = true
    // CHECK: function = @forwards_only, block = 1
    // CHECK-SAME: twoState = true
    // CHECK: function = @forwards_only, block = 2
    // CHECK-SAME: twoState = true
    // Direct call results and spawned value captures now use the whole-design
    // state-domain solution rather than being rejected by a local heuristic.
    // CHECK: function = @interproc_caller, block = 0
    // CHECK-SAME: twoState = true
    // CHECK: function = @interproc_unknown_caller, block = 0
    // CHECK-SAME: twoState = false
    // CHECK: function = @spawn_root, block = 0
    // CHECK-SAME: twoState = true
    // CHECK: function = @spawned_known, block = 0
    // CHECK-SAME: twoState = true
    simulation.func @forwards_only(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %loaded = simulation.ref.load %s : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^mid
    ^mid:
      simulation.suspend.delay %delay to ^last(%loaded : !simulation.logic<8>)
    ^last(%carried: !simulation.logic<8>):
      simulation.return
    }

    // The same value forwarded *and* consumed by a store, checked above.
    simulation.func @also_consumes(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %loaded = simulation.ref.load %s : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^next
    ^next:
      simulation.ref.store %loaded to %s : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^done(%loaded : !simulation.logic<8>)
    ^done(%carried: !simulation.logic<8>):
      simulation.return
    }

    simulation.func @known_callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %bits = arith.constant 23 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      simulation.return %known : !simulation.logic<8>
    }

    simulation.func @interproc_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %known = simulation.call @known_callee(%ctx) : (!simulation.context) -> !simulation.logic<8>
      %local = simulation.ref.alloc %known : !simulation.logic<8> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %known to %local : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    simulation.func @unknown_callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      simulation.return %unknown : !simulation.logic<8>
    }

    simulation.func @interproc_unknown_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      %unknown = simulation.call @unknown_callee(%ctx) : (!simulation.context) -> !simulation.logic<8>
      %local = simulation.ref.alloc %unknown : !simulation.logic<8> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %unknown to %local : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    simulation.func @spawn_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %bits = arith.constant 42 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %process = simulation.spawn @spawned_known(%ctx, %known) : !simulation.context, !simulation.logic<8> -> !simulation.process
      simulation.return
    }

    // Private visibility makes the spawn edge the complete set of entry uses;
    // a public process descriptor could be invoked with other capture values.
    simulation.func private @spawned_known(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %known: !simulation.logic<8> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %inverted = simulation.logic.unary bit_not %known : (!simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
  }
}
