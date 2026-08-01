// The boundary epilogue is a chain of argument-less blocks. When a region
// exits through a conditional branch, the operands belonging to the wait block
// must be held back and re-supplied on the branch that finally reaches it,
// exactly as the unconditional exit already does.
// RUN: obelisk-opt %s --obelisk-sim-optimize-native-regions | FileCheck %s

module {
  obelisk_sim.design @native_region {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "native_region.region"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func private @region(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %target: !obelisk_sim.ref<!obelisk_sim.logic<8>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32,
                    code_unit_id = 1 : i64,
                    obelisk.native.region_body} {
      %initial = arith.constant 0 : i32
      cf.br ^wait(%initial : i32)
    ^wait(%carried: i32):
      obelisk_sim.suspend.edge posedge %clock to ^body :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %first = obelisk_sim.logic.constant 1 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %first to %target {
        site = #obelisk_sim.nba_site<id = 0, commit = 7,
          storage = root_accumulator>
      } : (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %overwrite = arith.constant true
      // Both edges leave the activation, so both grow an epilogue.
      cf.cond_br %overwrite, ^overwrite, ^wait(%carried : i32)
    ^overwrite:
      %last = obelisk_sim.logic.constant 2 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %last to %target {
        site = #obelisk_sim.nba_site<id = 1, commit = 7,
          storage = root_accumulator>
      } : (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait(%carried : i32)
    }
  }
}

// Reaching a printable result at all is most of this test: forwarding the
// wait operands into the epilogue instead produced a branch whose operand
// count disagreed with its target, which the verifier rejects.
// CHECK-LABEL: obelisk_sim.func private @region
// CHECK: ^[[WAIT:bb[0-9]+]](%[[CARRIED:[^:]*]]: i32):
// CHECK: obelisk_sim.suspend.edge
// Each of the two exits stages its own last assignment, ...
// CHECK: obelisk_sim.nba.enqueue
// ... and every path back to the wait block restores its operand.
// CHECK: cf.br ^[[WAIT]](%[[CARRIED]] : i32)
// CHECK: obelisk_sim.nba.enqueue
// CHECK: cf.br ^[[WAIT]](%[[CARRIED]] : i32)
