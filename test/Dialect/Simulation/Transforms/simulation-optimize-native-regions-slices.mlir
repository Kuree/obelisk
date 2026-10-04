// RUN: obelisk-opt %s --split-input-file --obelisk-sim-optimize-native-regions | FileCheck %s

// Fixed disjoint views may forward independently; overlapping/dynamic views
// and transient observers must retain the original NBA staging sequence.

module {
  simulation.design @native_region attributes {
      schedule.nba.transient_observable = array<i64>,
      compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
        nodes = [#schedule.nba_commit<id = 7, slots = [],
          accumulatorSites = [0, 1, 2], frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
            descriptor = 0, formal = 0, low = 0, width = 16, dynamic = false,
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
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design

    simulation.func private @disjoint(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %target: !simulation.ref<!simulation.logic<16>>
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
      %low0 = simulation.ref.extract %target from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %high = simulation.ref.extract %target from 8 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %first = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %first to %low0 {
        site = #schedule.nba_site<id = 0, commit = 7,
          storage = root_accumulator>, test.staging_origin = 17 : i64
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.nba.enqueue %first to %high {site = #schedule.nba_site<id = 2, commit = 7, storage = root_accumulator>} : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %overwrite = arith.constant true
      cf.cond_br %overwrite, ^overwrite, ^join
    ^overwrite:
      %low1 = simulation.ref.extract %target from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %last = simulation.logic.constant 2 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %last to %low1 {
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

// CHECK-LABEL: simulation.func private @disjoint
// CHECK-COUNT-2: simulation.nba.enqueue
// CHECK-NOT: simulation.nba.enqueue

// -----

module {
  simulation.design @native_region attributes {
      schedule.nba.transient_observable = array<i64>,
      compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
        nodes = [#schedule.nba_commit<id = 7, slots = [],
          accumulatorSites = [0, 1, 2], frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
            descriptor = 0, formal = 0, low = 0, width = 16, dynamic = false,
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
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design

    simulation.func private @overlap(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %target: !simulation.ref<!simulation.logic<16>>
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
      %low0 = simulation.ref.extract %target from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %high = simulation.ref.extract %target from 8 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %first = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %first to %low0 {
        site = #schedule.nba_site<id = 0, commit = 7,
          storage = root_accumulator>, test.staging_origin = 17 : i64
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.nba.enqueue %first to %high {site = #schedule.nba_site<id = 2, commit = 7, storage = root_accumulator>} : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %overwrite = arith.constant true
      cf.cond_br %overwrite, ^overwrite, ^join
    ^overwrite:
      %low1 = simulation.ref.extract %target from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %last = simulation.logic.constant 2 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %last to %low1 {
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

// CHECK-LABEL: simulation.func private @overlap
// CHECK-COUNT-3: simulation.nba.enqueue
// CHECK-NOT: simulation.nba.enqueue

// -----

module {
  simulation.design @native_region attributes {
      schedule.nba.transient_observable = array<i64>,
      compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
        nodes = [#schedule.nba_commit<id = 7, slots = [],
          accumulatorSites = [0, 1, 2], frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
            descriptor = 0, formal = 0, low = 0, width = 16, dynamic = false,
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
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design

    simulation.func private @dynamic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %target: !simulation.ref<!simulation.logic<16>>
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
      %low0 = simulation.ref.extract %target from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %high = simulation.ref.extract %target from 8 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %first = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %first to %low0 {
        site = #schedule.nba_site<id = 0, commit = 7,
          storage = root_accumulator>, test.staging_origin = 17 : i64
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.nba.enqueue %first to %high {site = #schedule.nba_site<id = 2, commit = 7, storage = root_accumulator>} : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %overwrite = arith.constant true
      cf.cond_br %overwrite, ^overwrite, ^join
    ^overwrite:
      %index = arith.constant 0 : i64
      %low1 = simulation.ref.dyn_extract %target from %index : (!simulation.ref<!simulation.logic<16>>, i64) -> !simulation.ref<!simulation.logic<8>>
      %last = simulation.logic.constant 2 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %last to %low1 {
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

// CHECK-LABEL: simulation.func private @dynamic
// CHECK-COUNT-3: simulation.nba.enqueue
// CHECK-NOT: simulation.nba.enqueue

// -----

module {
  simulation.design @native_region attributes {
      schedule.nba.transient_observable = array<i64: 0>,
      compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
        nodes = [#schedule.nba_commit<id = 7, slots = [],
          accumulatorSites = [0, 1, 2], frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
            descriptor = 0, formal = 0, low = 0, width = 16, dynamic = false,
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
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design

    simulation.func private @observed(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %target: !simulation.ref<!simulation.logic<16>>
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
      %low0 = simulation.ref.extract %target from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %high = simulation.ref.extract %target from 8 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %first = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %first to %low0 {
        site = #schedule.nba_site<id = 0, commit = 7,
          storage = root_accumulator>, test.staging_origin = 17 : i64
      } : (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.nba.enqueue %first to %high {site = #schedule.nba_site<id = 2, commit = 7, storage = root_accumulator>} : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %overwrite = arith.constant true
      cf.cond_br %overwrite, ^overwrite, ^join
    ^overwrite:
      %low1 = simulation.ref.extract %target from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %last = simulation.logic.constant 2 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %last to %low1 {
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

// CHECK-LABEL: simulation.func private @observed
// CHECK-COUNT-3: simulation.nba.enqueue
// CHECK-NOT: simulation.nba.enqueue
