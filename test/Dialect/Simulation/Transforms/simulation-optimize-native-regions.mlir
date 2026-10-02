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
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body :
          !simulation.ref<!simulation.logic<1>>
    ^body:
      %first = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %first to %target {
        site = #schedule.nba_site<id = 0, commit = 7,
          storage = root_accumulator>, test.staging_origin = 17 : i64
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      %overwrite = arith.constant true
      cf.cond_br %overwrite, ^overwrite, ^join
    ^overwrite:
      %last = simulation.logic.constant 2 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %last to %target {
        site = #schedule.nba_site<id = 1, commit = 7,
          storage = root_accumulator>, test.staging_origin = 42 : i64
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^join
    ^join:
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: simulation.func private @region
// CHECK-NOT: schedule.native.region_body
// CHECK: ^{{.*}}(%{{.*}}: i1, %{{.*}}: !simulation.logic<8>):
// CHECK-COUNT-1: simulation.nba.enqueue
// CHECK-SAME: site = #schedule.nba_site<id = 1, commit = 7,
// CHECK-SAME: test.staging_origin = 42 : i64
