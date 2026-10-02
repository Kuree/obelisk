// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=read},simulation.func(canonicalize,mem2reg)))' | FileCheck %s --check-prefix=READ
module {
  simulation.design @bit_update {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 function hierarchy "copy"
    // The private state is loaded once and flushed once. Per-bit updates use
    // both-plane SSA and mem2reg, retaining invalid-index semantics.
    // CHECK-LABEL: simulation.func @copy
    // CHECK: simulation.ref.load
    // CHECK: simulation.logic.dyn_insert
    // CHECK: simulation.logic.dyn_insert
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // READ-LABEL: simulation.func @copy
    // READ: simulation.ref.store
    // READ: simulation.ref.store
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.logic<8> {simulation.capture_kind = 2 : i32}, %index: i32 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %one = arith.constant 1 : i32
      %next = arith.addi %index, %one : i32
      %a = simulation.logic.extract %source from 0 : !simulation.logic<8> -> !simulation.logic<1>
      %b = simulation.logic.extract %source from 1 : !simulation.logic<8> -> !simulation.logic<1>
      %first = simulation.ref.dyn_extract %root from %index : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %second = simulation.ref.dyn_extract %root from %next : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %a to %first : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %b to %second : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %value = simulation.ref.load %root : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : !simulation.logic<8>
    }
  }
}
