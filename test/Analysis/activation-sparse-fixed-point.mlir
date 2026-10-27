// RUN: talos-opt %s --test-talos-activation-memory='values=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// A runtime trip count does not get induction partitions. Rotating a wide SSA
// value around this backedge produces more lane correspondences than the sparse
// lattice's correlation budget. Widening must converge and retain every input
// bit, together with the trip count, without changing the executable loop.
module {
  simulation.design @sparse_fixed_point {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<257> design
    simulation.storage.decl 1 in 0 : i16 design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 function hierarchy "rotate"

    // CHECK-LABEL: activation @rotate valid=1 complete=1
    // CHECK: values valid=1
    // CHECK: value-input s0[0,257) s1[0,16){{$}}
    // CHECK: output s2[0,1) s0[0,257) s1[0,16){{$}}
    simulation.func @rotate(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<257>>
      %count = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<257>> -> !simulation.logic<257>
      %trips = simulation.ref.load %count : !simulation.ref<i16> -> i16
      %zero = arith.constant 0 : i16
      %one = arith.constant 1 : i16
      cf.br ^loop(%trips, %value : i16, !simulation.logic<257>)
    ^loop(%left: i16, %current: !simulation.logic<257>):
      %done = arith.cmpi eq, %left, %zero : i16
      cf.cond_br %done, ^exit, ^body
    ^body:
      %low = simulation.logic.extract %current from 0 : !simulation.logic<257> -> !simulation.logic<256>
      %high = simulation.logic.extract %current from 256 : !simulation.logic<257> -> !simulation.logic<1>
      %shifted = simulation.logic.insert %low into %current at 1 : (!simulation.logic<257>, !simulation.logic<256>) -> !simulation.logic<257>
      %rotated = simulation.logic.insert %high into %shifted at 0 : (!simulation.logic<257>, !simulation.logic<1>) -> !simulation.logic<257>
      %next = arith.subi %left, %one : i16
      cf.br ^loop(%next, %rotated : i16, !simulation.logic<257>)
    ^exit:
      %bit = simulation.logic.extract %current from 0 : !simulation.logic<257> -> !simulation.logic<1>
      simulation.ref.store %bit to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
