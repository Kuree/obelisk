// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-annotate-steady-unknown{vpi=off}))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-annotate-steady-unknown{vpi=full}))' | FileCheck %s --check-prefix=WRITABLE

// The high byte of `half` is never written, so it keeps its initial X
// (IEEE 1800-2023 6.8): its steady pattern. The stale fact on `flag`, which
// the design stores a known value into, is replaced. A writable VPI client
// may deposit anything, so it records nothing.

// CHECK: simulation.storage.decl 0 in 0 : !simulation.logic<16> design {simulation.steady_unknown = array<i64: 8, 8>}
// CHECK-NEXT: simulation.storage.decl 1 in 0 : !simulation.logic<1> design{{$}}
// WRITABLE-NOT: steady_unknown

!c = !simulation.context
!l1 = !simulation.logic<1>
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
module {
  simulation.design @annotate {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l16 design
    simulation.storage.decl 1 in 0 : !l1 design {simulation.steady_unknown = array<i64: 0, 1>}
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %half = simulation.context.storage %ctx[0] : !simulation.ref<!l16>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<!l1>
      %one = simulation.logic.constant 1 : i1, 0 : i1 : !l1
      simulation.ref.store %one to %flag : !l1, !simulation.ref<!l1>
      %f = simulation.ref.load %flag : !simulation.ref<!l1> -> !l1
      %v = simulation.logic.resize %f signed = false : !l1 -> !l8
      %low = simulation.ref.extract %half from 0 : !simulation.ref<!l16> -> !simulation.ref<!l8>
      simulation.ref.store %v to %low : !l8, !simulation.ref<!l8>
      simulation.return
    }
  }
}
