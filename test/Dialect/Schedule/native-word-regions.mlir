// RUN: obelisk-opt %s --split-input-file --schedule-optimize-native-words | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --schedule-optimize-native-words --convert-schedule-word-regions --canonicalize | FileCheck %s --check-prefix=LOWER
// Native width is justified by bounded lane dataflow, independently of X/Z.
// The input has no knownness certificate: XOR must retain both planes.
module {
  simulation.design @word attributes {
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
    // CHECK-LABEL: simulation.func @word
    // CHECK: schedule.word.region 8
    // CHECK: simulation.ref.dyn_extract
    // CHECK: simulation.ref.load {{.*}} -> !simulation.logic<8>
    // CHECK: simulation.logic.binary xor {{.*}} : !simulation.logic<8>
    // CHECK: simulation.ref.store {{.*}} : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
    // CHECK: schedule.word.yield
    // LOWER-LABEL: simulation.func @word
    // LOWER-NOT: schedule.word
    // LOWER-NOT: cf.
    // LOWER: simulation.logic.binary xor {{.*}} : !simulation.logic<8>
    // LOWER: simulation.ref.store
    // LOWER-NOT: simulation.ref.store
    // LOWER: simulation.return
    simulation.func @word(
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

// Conditional entry, a runtime-invariant offset, and nested independent ways.
// The offset need not be constant to preserve outer-lane independence.
// Capture specialization can rematerialize a fixed storage lookup in a loop.
// CHECK-LABEL: simulation.func @nested
// CHECK: schedule.word.region 8
// CHECK: cf.cond_br
// CHECK-DAG: simulation.logic.binary xor {{.*}} : !simulation.logic<8>
// CHECK-DAG: schedule.word.yield
// LOWER-LABEL: simulation.func @nested
// LOWER-NOT: schedule.word
// LOWER: simulation.logic.binary xor {{.*}} : !simulation.logic<8>
module {
  simulation.design @nested attributes {
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
    simulation.storage.decl 0 in 0 : !simulation.packed_array<23 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<23 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<23 : 0 x !simulation.logic<1>> design
    simulation.func @nested(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      %packed = simulation.ref.load %a : !simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<23 : 0 x !simulation.logic<1>>
      %flat = simulation.packed.flatten %packed : (!simulation.packed_array<23 : 0 x !simulation.logic<1>>) -> !simulation.logic<24>
      %bits = simulation.logic.to_bits %flat : !simulation.logic<24> -> i24
      %seven = arith.constant 7 : i24
      %bounded = arith.andi %bits, %seven : i24
      %bias = arith.extui %bounded : i24 to i32
      %yes = arith.constant true
      cf.cond_br %yes, ^head(%zero, %bias : i32, i32), ^exit
    ^head(%i: i32, %offset: i32):
      %limit = arith.constant 8 : i32
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
      %base = arith.muli %j, %eight : i32
      %biased = arith.addi %base, %offset : i32
      %lane = arith.addi %biased, %i : i32
      %index = arith.extui %lane : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %fixed = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>>
      %dst = simulation.ref.array_element %fixed[%index] : (!simulation.ref<!simulation.packed_array<23 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %inner_one = arith.constant 1 : i32
      %inner_next = arith.addi %j, %inner_one : i32
      cf.br ^inner(%inner_next : i32)
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next, %offset : i32, i32)
    ^exit:
      simulation.return
    }
  }
}

// -----

// Largest admitted native word.
// CHECK-LABEL: simulation.func @maximum
// CHECK: schedule.word.region 64
// CHECK: simulation.logic.binary xor {{.*}} : !simulation.logic<64>
// LOWER-LABEL: simulation.func @maximum
// LOWER-NOT: cf.
// LOWER: simulation.logic.binary xor {{.*}} : !simulation.logic<64>
module {
  simulation.design @maximum attributes {
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
    simulation.storage.decl 0 in 0 : !simulation.packed_array<63 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<63 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<63 : 0 x !simulation.logic<1>> design
    simulation.func @maximum(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<63 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<63 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<63 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<63 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<63 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<63 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
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

// Reanalysis must retain capture facts across a previously formed region.
// CHECK-LABEL: simulation.func @two_regions
// CHECK-COUNT-2: schedule.word.region 8
// LOWER-LABEL: simulation.func @two_regions
// LOWER-COUNT-2: simulation.ref.store
// LOWER: simulation.return
module {
  simulation.design @two_regions attributes {
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
    simulation.func @two_regions(
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
      cf.br ^head2(%zero : i32)
    ^head2(%i2: i32):
      %limit2 = arith.constant 8 : i32
      %test2 = arith.cmpi ult, %i2, %limit2 : i32
      cf.cond_br %test2, ^body2, ^exit2
    ^body2:
      %index2 = arith.extui %i2 : i32 to i65
      %ar2 = simulation.ref.array_element %a[%index2] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br2 = simulation.ref.array_element %b[%index2] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av2 = simulation.ref.load %ar2 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv2 = simulation.ref.load %br2 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v2 = simulation.logic.binary xor %av2, %bv2 : !simulation.logic<1>
      %dst2 = simulation.ref.array_element %out[%index2] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v2 to %dst2 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one2 = arith.constant 1 : i32
      %next2 = arith.addi %i2, %one2 : i32
      cf.br ^head2(%next2 : i32)
    ^exit2:
      simulation.return
    }
  }
}


// -----
// Ordered repeated writes to one identical lane map remain independent.
// CHECK-LABEL: simulation.func @repeated_write
// CHECK: schedule.word.region 8
// CHECK-COUNT-2: simulation.ref.store
// LOWER-LABEL: simulation.func @repeated_write
// LOWER-COUNT-2: simulation.ref.store


module {
  simulation.design @repeated_write attributes {
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
    simulation.func @repeated_write(
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
      simulation.ref.store %av to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}


// -----

// A 65-lane loop uses five complete 13-bit groups.
// CHECK-LABEL: simulation.func @chunked_65
// CHECK: cf.cond_br
// CHECK: schedule.word.region 13
// CHECK: simulation.ref.store
// CHECK: simulation.return
module {
  simulation.design @chunked_65 attributes {
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
    simulation.storage.decl 0 in 0 : !simulation.packed_array<64 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<64 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<64 : 0 x !simulation.logic<1>> design
    simulation.func @chunked_65(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<64 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<64 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<64 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 65 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<64 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<64 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<64 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
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

// A prime trip count uses a native word plus a smaller exact word tail.
// CHECK-LABEL: simulation.func @prime_trip_count
// CHECK: schedule.word.region 64
// CHECK: schedule.word.region 3
// LOWER-LABEL: simulation.func @prime_trip_count
// LOWER-NOT: schedule.word
// LOWER: simulation.logic.binary xor {{.*}}!simulation.logic<64>
// LOWER: simulation.logic.binary xor {{.*}}!simulation.logic<3>
module {
  simulation.design @prime_trip_count attributes {
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
    simulation.storage.decl 0 in 0 : !simulation.packed_array<66 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<66 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<66 : 0 x !simulation.logic<1>> design
    simulation.func @prime_trip_count(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<66 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<66 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<66 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 67 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %ar = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<66 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %br = simulation.ref.array_element %b[%index] : (!simulation.ref<!simulation.packed_array<66 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %av = simulation.ref.load %ar : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %bv = simulation.ref.load %br : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v = simulation.logic.binary xor %av, %bv : !simulation.logic<1>
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.packed_array<66 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
