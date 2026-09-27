// The boundary epilogue is a chain of argument-less blocks. When a region
// exits through a conditional branch, the operands belonging to the wait block
// must be held back and re-supplied on the branch that finally reaches it,
// exactly as the unconditional exit already does.
// RUN: obelisk-opt %s --obelisk-sim-optimize-native-regions | FileCheck %s

module {
  simulation.design @native_region attributes {
      schedule.nba.transient_observable = array<i64>,
      compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
        nodes = [#schedule.nba_commit<id = 7, slots = [],
          accumulatorSites = [0, 1], frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
            descriptor = 0, formal = 0, low = 0, width = 8, dynamic = false,
            deferred = false, trigger = none>>],
        edges = [], regions = [
          #schedule.region<kind = active, groups = []>,
          #schedule.region<kind = nba, groups = [
            #schedule.group<fragments = [7], schedule = acyclic,
                               feedback = []>]>,
          #schedule.region<kind = observed, groups = []>,
          #schedule.region<kind = reactive, groups = []>,
          #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always hierarchy "native_region.region"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design

    simulation.func private @region(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %target: !simulation.ref<!simulation.logic<8>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32,
                    code_unit_id = 1 : i64,
                    schedule.native.region_body} {
      %initial = arith.constant 0 : i32
      cf.br ^wait(%initial : i32)
    ^wait(%carried: i32):
      simulation.suspend.edge posedge %clock to ^body :
          !simulation.ref<!simulation.logic<1>>
    ^body:
      %first = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %first to %target {
        site = #schedule.nba_site<id = 0, commit = 7,
          storage = root_accumulator>
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      %overwrite = arith.constant true
      // Both edges leave the activation, so both grow an epilogue.
      cf.cond_br %overwrite, ^overwrite, ^wait(%carried : i32)
    ^overwrite:
      %last = simulation.logic.constant 2 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %last to %target {
        site = #schedule.nba_site<id = 1, commit = 7,
          storage = root_accumulator>
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait(%carried : i32)
    }
  }
}

// Reaching a printable result at all is most of this test: forwarding the
// wait operands into the epilogue instead produced a branch whose operand
// count disagreed with its target, which the verifier rejects.
// CHECK-LABEL: simulation.func private @region
// CHECK: ^[[WAIT:bb[0-9]+]](%[[CARRIED:[^:]*]]: i32):
// CHECK: simulation.suspend.edge
// Each of the two exits stages its own last assignment, ...
// CHECK: simulation.nba.enqueue
// ... and every path back to the wait block restores its operand.
// CHECK: cf.br ^[[WAIT]](%[[CARRIED]] : i32)
// CHECK: simulation.nba.enqueue
// CHECK: cf.br ^[[WAIT]](%[[CARRIED]] : i32)
