// RUN: obelisk-opt %s --mem2reg | FileCheck %s --check-prefix=UNSAFE
// RUN: obelisk-opt %s --mem2reg | FileCheck %s --check-prefix=RESET

module {
  obelisk_sim.design @design {
    obelisk_sim.code_unit.decl 1 in 0 always_comb hierarchy "reinitialized_loop"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @unsafe_reentry(
        %context: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %watched: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body

    ^body:
      %zero = arith.constant 0 : i32
      %local = obelisk_sim.ref.alloc %zero : i32 -> !obelisk_sim.ref<i32>
      %value = obelisk_sim.ref.load %local : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %value, %one : i32
      obelisk_sim.ref.store %next to %local : i32, !obelisk_sim.ref<i32>
      obelisk_sim.suspend.change %watched to ^body : !obelisk_sim.ref<i1>
    }
  }
}

// UNSAFE-LABEL: obelisk_sim.func @unsafe_reentry
// UNSAFE: ^bb1:
// UNSAFE: %[[ZERO:.*]] = arith.constant 0 : i32
// UNSAFE: %[[LOCAL:.*]] = obelisk_sim.ref.alloc %[[ZERO]] : i32 -> !obelisk_sim.ref<i32>
// UNSAFE: %[[VALUE:.*]] = obelisk_sim.ref.load %[[LOCAL]]
// UNSAFE: obelisk_sim.ref.store {{%.*}} to %[[LOCAL]]
// UNSAFE: obelisk_sim.suspend.change %arg1 to ^bb1

// -----

module {
  obelisk_sim.design @design {
    obelisk_sim.code_unit.decl 1 in 0 always_comb hierarchy "reinitialized_loop"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @reinitialized_loop(
        %context: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %watched: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body

    ^body:
      %zero = arith.constant 0 : i32
      %local = obelisk_sim.ref.alloc %zero : i32 -> !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %zero to %local : i32, !obelisk_sim.ref<i32>
      %value = obelisk_sim.ref.load %local : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %value, %one : i32
      obelisk_sim.ref.store %next to %local : i32, !obelisk_sim.ref<i32>
      obelisk_sim.suspend.change %watched to ^body : !obelisk_sim.ref<i1>
    }
  }
}

// RESET-LABEL: obelisk_sim.func @reinitialized_loop
// RESET-NOT: obelisk_sim.ref.alloc
// RESET-NOT: obelisk_sim.ref.load
// RESET-NOT: obelisk_sim.ref.store
// RESET: obelisk_sim.suspend.change %arg1 to ^bb1
