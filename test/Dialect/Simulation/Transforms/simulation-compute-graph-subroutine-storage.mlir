// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph))' \
// RUN:   | FileCheck %s

// IEEE 1800-2017 6.5 restricts the drivers of a variable, not the storage a
// called subroutine owns. Two continuous assignments may call the same static
// function, so writes to that function's own storage are not competing
// drivers.
module {
  simulation.design @subroutine_storage {
    simulation.code_unit.decl 9300001 in 0 continuous
        hierarchy "top.first"
    simulation.code_unit.decl 9300002 in 0 continuous
        hierarchy "top.second"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design hierarchy "top.invert.invert"
        {simulation.subroutine_storage}

    simulation.func @first(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300001 : i64} {
      %constant = arith.constant 12 : i32
      simulation.ref.store %constant to %value : i32, !simulation.ref<i32>
      simulation.return
    }

    simulation.func @second(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300002 : i64} {
      %constant = arith.constant 13 : i32
      simulation.ref.store %constant to %value : i32, !simulation.ref<i32>
      simulation.return
    }
  }
}

// CHECK: simulation.design @subroutine_storage
// CHECK-SAME: compute_graph
