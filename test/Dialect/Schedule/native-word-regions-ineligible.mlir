// RUN: talos-opt %s --split-input-file --schedule-optimize-native-words | FileCheck %s --implicit-check-not=schedule.word.region

// Reversing an aliased destination creates a cross-lane recurrence.
// CHECK-LABEL: simulation.func @alias
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @alias attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @alias(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %last = arith.constant 7 : i65
      %back = arith.subi %last, %index : i65
      %dst = simulation.ref.array_element %a[%back] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// Intermediate writes cannot be removed for a value-sequence observer.
// CHECK-LABEL: simulation.func @observer
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @observer attributes {
    schedule.nba.transient_observable = array<i64: 2>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @observer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// A root needs the independent observer influence certificate.
// CHECK-LABEL: simulation.func @influence
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @influence attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @influence(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// The final lane is out of bounds, so a word access is not equivalent.
// CHECK-LABEL: simulation.func @bounds
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @bounds attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @bounds(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 9 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// An unresolved capture might alias a destination.
// CHECK-LABEL: simulation.func @unknown_root
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @unknown_root attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @unknown_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 1 : i32},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// Selecting different control flow per bit requires predication.
// CHECK-LABEL: simulation.func @lane_branch
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @lane_branch attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @lane_branch(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %gate = arith.cmpi eq, %i, %zero : i32
      cf.cond_br %gate, ^compute, ^advance
    ^compute:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^advance
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// Modulo arithmetic is not consecutive affine addressing.
// CHECK-LABEL: simulation.func @wrapped_index
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @wrapped_index attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @wrapped_index(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %four = arith.constant 4 : i32
      %mod = arith.remui %i, %four : i32
      %index = arith.extui %mod : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// A varying loop-carried accumulator is not a lane-uniform capture.
// CHECK-LABEL: simulation.func @loop_carried_value
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @loop_carried_value attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @loop_carried_value(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero, %zero : i32, i32)
    ^head(%i: i32, %sum: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      %sum_next = arith.addi %sum, %one : i32
      cf.br ^head(%next, %sum_next : i32, i32)
    ^exit:
      %fmt = simulation.bytes.constant "index %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %sum) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i32
      simulation.return
    }
  }
}

// -----

// VPI may observe intermediate values.
// CHECK-LABEL: simulation.func @vpi
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @vpi attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = full, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @vpi(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// Every wrapped selector is in bounds, but 252,253,254,255,0,1,2,3
// is not a contiguous word. Reference bounds alone cannot prove admission.
// CHECK-LABEL: simulation.func @integer_wrap
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @integer_wrap attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<255 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<255 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<255 : 0 x !simulation.logic<1>> design
    simulation.func @integer_wrap(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<255 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<255 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<255 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i8
      cf.br ^head(%zero : i8)
    ^head(%i: i8):
      %limit = arith.constant 8 : i8
      %test = arith.cmpi ult, %i, %limit : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      %bias = arith.constant -4 : i8
      %wrapped = arith.addi %i, %bias : i8
      %index = arith.extui %wrapped : i8 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<255 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<255 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<255 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i8
      %next = arith.addi %i, %one : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }
  }
}

// -----

// Do not speculate operations with undefined scalar rows.
// CHECK-LABEL: simulation.func @guarded_division
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @guarded_division attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @guarded_division(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %ai = simulation.logic.to_bits %av : !simulation.logic<1> -> i1
      %bi = simulation.logic.to_bits %bv : !simulation.logic<1> -> i1
      cf.cond_br %bi, ^divide, ^advance
    ^divide:
      %q = arith.divui %ai, %bi : i1
      %v = simulation.logic.from_bits %q : i1 -> !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^advance
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// A conditional cross-lane recurrence still cannot be reordered.
// CHECK-LABEL: simulation.func @conditional_recurrence
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @conditional_recurrence attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.func @conditional_recurrence(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %condition = simulation.logic.is_true %bv : !simulation.logic<1>
      cf.cond_br %condition, ^write, ^advance
    ^write:
      %last = arith.constant 7 : i65
      %reverse = arith.subi %last, %index : i65
      %dst = simulation.ref.array_element %a[%reverse] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^advance
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// The same store crosses outer lanes on different inner iterations.
// A single static SSA reference is not proof of self-independence.
// A zero address residue is not a loop-invariant value: division exposes
// high bits of the inner index and must not inherit a symbolic fixed offset.
// CHECK-LABEL: simulation.func @nested_overlap
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @nested_overlap attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<15 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<15 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<15 : 0 x !simulation.logic<1>> design
    simulation.func @nested_overlap(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      %yes = arith.constant true
      cf.cond_br %yes, ^head(%zero : i32), ^exit
    ^head(%i: i32):
      %limit = arith.constant 3 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %two = arith.constant 2 : i32
      %inner_test = arith.cmpi ult, %j, %two : i32
      cf.cond_br %inner_test, ^inner_body, ^advance
    ^inner_body:
      %eight = arith.constant 8 : i32
      %multiple = arith.muli %j, %eight : i32
      %four = arith.constant 4 : i32
      %base = arith.divui %multiple, %four : i32
      %lane = arith.addi %base, %i : i32
      %index = arith.extui %lane : i32 to i65
      %input_index = arith.extui %j : i32 to i65
      %ar = simulation.ref.array_element %a[%input_index] : (!simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%input_index] : (!simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %inner_one = arith.constant 1 : i32
      %inner_next = arith.addi %j, %inner_one : i32
      cf.br ^inner(%inner_next : i32)
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// An SSA array read extends past the source, although every destination is valid.
// CHECK-LABEL: simulation.func @ssa_bounds
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @ssa_bounds attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<8 : 0 x !simulation.logic<1>> design
    simulation.func @ssa_bounds(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<8 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 9 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %snapshot = simulation.ref.load %a : !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
      %v = simulation.array.extract_dynamic %snapshot[%index] : (!simulation.packed_array<7 : 0 x !simulation.logic<1>>, i65) -> !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<8 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// An SSA array read extends past the source, although every destination is valid.
// CHECK-LABEL: simulation.func @unpacked_ssa_bounds
// CHECK-NOT: schedule.word.region
// CHECK: cf.cond_br
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @unpacked_ssa_bounds attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 0, 1, 2>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<7 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<8 : 0 x !simulation.logic<1>> design
    simulation.func @unpacked_ssa_bounds(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.unpacked_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.unpacked_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.unpacked_array<8 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 9 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %snapshot = simulation.ref.load %a : !simulation.ref<!simulation.unpacked_array<7 : 0 x !simulation.logic<1>>> -> !simulation.unpacked_array<7 : 0 x !simulation.logic<1>>
      %v = simulation.array.extract_dynamic %snapshot[%index] : (!simulation.unpacked_array<7 : 0 x !simulation.logic<1>>, i65) -> !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.unpacked_array<8 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
