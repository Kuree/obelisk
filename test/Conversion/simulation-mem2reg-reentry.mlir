// RUN: obelisk-opt %s --mem2reg | FileCheck %s --check-prefix=UNSAFE
// RUN: obelisk-opt %s --mem2reg | FileCheck %s --check-prefix=RESET
// RUN: obelisk-opt %s --canonicalize --mem2reg | FileCheck %s --check-prefix=INIT

// LRM 6.21: canonicalization exposes automatic initialization on every entry,
// so promotion cannot accidentally retain a prior activation's value.
// INIT-LABEL: simulation.func @unsafe_reentry
// INIT-NOT: simulation.ref.alloc
// INIT-NOT: simulation.ref.load
// INIT-NOT: simulation.ref.store
// INIT: simulation.suspend.change %arg1
// INIT-LABEL: simulation.func @reinitialized_loop
// INIT-NOT: simulation.ref.alloc
// INIT-NOT: simulation.ref.load
// INIT-NOT: simulation.ref.store
// INIT: simulation.suspend.change %arg1

module {
  simulation.design @design {
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "reinitialized_loop"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @unsafe_reentry(
        %context: !simulation.context {simulation.capture_kind = 0 : i32},
        %watched: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body

    ^body:
      %zero = arith.constant 0 : i32
      %local = simulation.ref.alloc %zero : i32 -> !simulation.ref<i32>
      %value = simulation.ref.load %local : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %value, %one : i32
      simulation.ref.store %next to %local : i32, !simulation.ref<i32>
      simulation.suspend.change %watched to ^body : !simulation.ref<i1>
    }
  }
}

// UNSAFE-LABEL: simulation.func @unsafe_reentry
// UNSAFE: ^bb1:
// UNSAFE: %[[ZERO:.*]] = arith.constant 0 : i32
// UNSAFE: %[[LOCAL:.*]] = simulation.ref.alloc %[[ZERO]] : i32 -> !simulation.ref<i32>
// UNSAFE: %[[VALUE:.*]] = simulation.ref.load %[[LOCAL]]
// UNSAFE: simulation.ref.store {{%.*}} to %[[LOCAL]]
// UNSAFE: simulation.suspend.change %arg1 to ^bb1

// -----

module {
  simulation.design @design {
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "reinitialized_loop"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @reinitialized_loop(
        %context: !simulation.context {simulation.capture_kind = 0 : i32},
        %watched: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body

    ^body:
      %zero = arith.constant 0 : i32
      %local = simulation.ref.alloc %zero : i32 -> !simulation.ref<i32>
      simulation.ref.store %zero to %local : i32, !simulation.ref<i32>
      %value = simulation.ref.load %local : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %value, %one : i32
      simulation.ref.store %next to %local : i32, !simulation.ref<i32>
      simulation.suspend.change %watched to ^body : !simulation.ref<i1>
    }
  }
}

// RESET-LABEL: simulation.func @reinitialized_loop
// RESET-NOT: simulation.ref.alloc
// RESET-NOT: simulation.ref.load
// RESET-NOT: simulation.ref.store
// RESET: simulation.suspend.change %arg1 to ^bb1
