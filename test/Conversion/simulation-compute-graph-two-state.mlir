// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // A fragment is two-state only when no four-state value it produces or
  // consumes can hold X or Z. The suspension terminator is the one exemption:
  // it forwards the frame rather than computing with it, and the resuming
  // fragment proves those values itself. That exemption belongs to the
  // terminator alone.
  obelisk_sim.design @two_state {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.two_state.forwards_only.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.two_state.also_consumes.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.two_state.known_callee.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.two_state.interproc_caller.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 function hierarchy "test.two_state.unknown_callee.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 initial hierarchy "test.two_state.interproc_unknown_caller.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 initial hierarchy "test.two_state.spawned_known.9000007"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    // Graph nodes are ordered by function symbol, so @also_consumes is first.
    // Its middle fragment both stores and forwards the same unproven value.
    // The store has no result, so only the operand check can see it: the
    // exemption must not leak from the terminator to the rest of the block.
    // CHECK: function = @also_consumes, block = 1
    // CHECK-SAME: twoState = false

    // A loaded value is unproven, so the block that loads it is four-state,
    // and so is the block that receives it as an argument. The fragment in
    // between only passes it along, and is two-state.
    // CHECK: function = @forwards_only, block = 0
    // CHECK-SAME: twoState = false
    // CHECK: function = @forwards_only, block = 1
    // CHECK-SAME: twoState = true
    // CHECK: function = @forwards_only, block = 2
    // CHECK-SAME: twoState = false
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
    obelisk_sim.func @forwards_only(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %loaded = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^mid
    ^mid:
      obelisk_sim.suspend.delay %delay to ^last(%loaded : !obelisk_sim.logic<8>)
    ^last(%carried: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }

    // The same value forwarded *and* consumed by a store, checked above.
    obelisk_sim.func @also_consumes(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %loaded = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^next
    ^next:
      obelisk_sim.ref.store %loaded to %s : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.delay %delay to ^done(%loaded : !obelisk_sim.logic<8>)
    ^done(%carried: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }

    obelisk_sim.func @known_callee(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %bits = arith.constant 23 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      obelisk_sim.return %known : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @interproc_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %known = obelisk_sim.call @known_callee(%ctx) : (!obelisk_sim.context) -> !obelisk_sim.logic<8>
      %local = obelisk_sim.ref.alloc %known : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %known to %local : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    obelisk_sim.func @unknown_callee(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.return %unknown : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @interproc_unknown_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      %unknown = obelisk_sim.call @unknown_callee(%ctx) : (!obelisk_sim.context) -> !obelisk_sim.logic<8>
      %local = obelisk_sim.ref.alloc %unknown : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %unknown to %local : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    obelisk_sim.func @spawn_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %bits = arith.constant 42 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %process = obelisk_sim.spawn @spawned_known(%ctx, %known) : !obelisk_sim.context, !obelisk_sim.logic<8> -> !obelisk_sim.process
      obelisk_sim.return
    }

    // Private visibility makes the spawn edge the complete set of entry uses;
    // a public process descriptor could be invoked with other capture values.
    obelisk_sim.func private @spawned_known(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %known: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %inverted = obelisk_sim.logic.unary bit_not %known : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}
