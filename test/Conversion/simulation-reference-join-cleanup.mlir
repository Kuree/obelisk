// RUN: obelisk-opt %s '--pass-pipeline=builtin.module(simulation.design(simulation.func(schedule-specialize-reference-joins,schedule-optimize-native-aggregates)))' | FileCheck %s
// Array cleanup must preserve the separate storage identities established for
// native lowering. Aggressive identical-block merging would recreate a ref phi.
// CHECK-LABEL: simulation.func @preserve_destinations
// CHECK: simulation.array.insert_dynamic
// CHECK: cf.cond_br %arg3, ^bb{{[0-9]+}}, ^bb{{[0-9]+}}{{$}}
// CHECK-DAG: simulation.ref.store %arg4 to %arg1
// CHECK-DAG: simulation.ref.store %arg4 to %arg2
// CHECK: simulation.return
!array = !simulation.unpacked_array<0 : 1 x i8>
module {
  simulation.design @cleanup {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "preserve_destinations"
    simulation.func @preserve_destinations(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %left: !simulation.ref<i8> {simulation.capture_kind = 2 : i32},
        %right: !simulation.ref<i8> {simulation.capture_kind = 2 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32},
        %initial: !array {simulation.capture_kind = 2 : i32},
        %index: i65 {simulation.capture_kind = 2 : i32},
        %out: !simulation.ref<!array> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %updated = simulation.array.insert_dynamic %value into %initial[%index] : (!array, i8, i65) -> !array
      simulation.ref.store %updated to %out : !array, !simulation.ref<!array>
      cf.cond_br %choose, ^write(%left : !simulation.ref<i8>), ^write(%right : !simulation.ref<i8>)
    ^write(%selected: !simulation.ref<i8>):
      simulation.ref.store %value to %selected : i8, !simulation.ref<i8>
      cf.br ^done
    ^done:
      simulation.return
    }
  }
}
