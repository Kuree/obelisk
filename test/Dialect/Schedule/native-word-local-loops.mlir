// RUN: obelisk-opt %s --schedule-optimize-native-words | FileCheck %s
// RUN: obelisk-opt %s --schedule-optimize-native-words='predication-operation-budget=0' | FileCheck %s
// A fused function can mix invocation-local loops with canonical storage loops.
// Refusing the former must leave their effects intact and still widen the latter.
// CHECK-LABEL: simulation.func @local_then_canonical
// CHECK: simulation.ref.alloc
// CHECK: cf.cond_br
// CHECK: simulation.ref.store {{.*}} : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
// CHECK: schedule.word.region 8
// CHECK: simulation.logic.binary xor {{.*}} : !simulation.logic<8>
// CHECK: simulation.ref.store {{.*}} : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
// CHECK: schedule.word.yield
// CHECK: simulation.return
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
    simulation.func @local_then_canonical(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      %initial = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %array = simulation.packed.unflatten %initial : (!simulation.logic<8>) -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
      %local = simulation.ref.alloc %array : !simulation.packed_array<7 : 0 x !simulation.logic<1>> -> !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
      cf.br ^local_head(%zero : i32)
    ^local_head(%j: i32):
      %local_limit = arith.constant 8 : i32
      %local_test = arith.cmpi ult, %j, %local_limit : i32
      cf.cond_br %local_test, ^local_body, ^canonical
    ^local_body:
      %local_index = arith.extui %j : i32 to i65
      %local_dst = simulation.ref.array_element %local[%local_index] : (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %local_value = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.ref.store %local_value to %local_dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %local_one = arith.constant 1 : i32
      %local_next = arith.addi %j, %local_one : i32
      cf.br ^local_head(%local_next : i32)
    ^canonical:
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
