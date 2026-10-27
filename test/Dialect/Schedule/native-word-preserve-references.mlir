// RUN: talos-opt %s '--pass-pipeline=builtin.module(simulation.design(simulation.func(schedule-specialize-reference-joins)),schedule-optimize-native-words)' | FileCheck %s
// Predicating a loop in a fused body must preserve storage alternatives in
// unrelated blocks. Native lowering requires their separate root identities.
// CHECK-LABEL: simulation.func @preserve_references
// CHECK: schedule.word.region 8
// CHECK: schedule.word.yield
// CHECK: cf.cond_br %arg4, ^bb{{[0-9]+}}, ^bb{{[0-9]+}}{{$}}
// CHECK-DAG: simulation.ref.store %{{.*}} to %arg1
// CHECK-DAG: simulation.ref.store %{{.*}} to %arg2
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
    simulation.func @preserve_references(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %choose: i1 {simulation.capture_kind = 2 : i32})
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
      %condition = simulation.logic.is_true %av : !simulation.logic<1>
      cf.cond_br %condition, ^yes, ^no
    ^yes:
      simulation.ref.store %av to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^advance
    ^no:
      simulation.ref.store %bv to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^advance
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      %value = simulation.ref.load %out : !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
      cf.cond_br %choose, ^store(%a : !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>), ^store(%b : !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>)
    ^store(%selected: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>):
      simulation.ref.store %value to %selected : !simulation.packed_array<7 : 0 x !simulation.logic<1>>, !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
      cf.br ^done
    ^done:
      simulation.return
    }
  }
}
