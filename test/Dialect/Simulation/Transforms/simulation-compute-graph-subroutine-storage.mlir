// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph))' \
// RUN:   | FileCheck %s

// IEEE 1800-2017 6.5 restricts the drivers of a variable, not the storage a
// called subroutine owns. Two continuous assignments may call the same static
// function, so writes to that function's own storage are not competing
// drivers.
module {
  obelisk_sim.design @subroutine_storage {
    obelisk_sim.code_unit.decl 9300001 in 0 continuous
        hierarchy "top.first"
    obelisk_sim.code_unit.decl 9300002 in 0 continuous
        hierarchy "top.second"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i32 design hierarchy "top.invert.invert"
        {obelisk_sim.subroutine_storage}

    obelisk_sim.func @first(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300001 : i64} {
      %constant = arith.constant 12 : i32
      obelisk_sim.ref.store %constant to %value : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }

    obelisk_sim.func @second(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300002 : i64} {
      %constant = arith.constant 13 : i32
      obelisk_sim.ref.store %constant to %value : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.design @subroutine_storage
// CHECK-SAME: compute_graph
