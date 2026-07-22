// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // A fragment is two-state only when no four-state value it produces or
  // consumes can hold X or Z. The suspension terminator is the one exemption:
  // it forwards the frame rather than computing with it, and the resuming
  // fragment proves those values itself. That exemption belongs to the
  // terminator alone.
  obelisk_sim.design @two_state {
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
    obelisk_sim.func @forwards_only(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32} {
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
        attributes {entry_kind = 1 : i32} {
      %loaded = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^next
    ^next:
      obelisk_sim.ref.store %loaded to %s : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.delay %delay to ^done(%loaded : !obelisk_sim.logic<8>)
    ^done(%carried: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }
  }
}
