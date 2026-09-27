// RUN: %split-file %s %t
// RUN: obelisk-opt %t/narrow-assignment.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NARROW
// RUN: obelisk-opt %t/wide-assignment.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=WIDE

// NARROW-LABEL: llvm.func @solve(
// NARROW: %[[CAPTURE_WIDTH:.*]] = llvm.mlir.constant(64 : i32)
// NARROW: llvm.store %[[CAPTURE_WIDTH]],
// NARROW: llvm.call @obelisk_rt_v1_random_solve_wide_modes_state
// NARROW: llvm.trunc {{.*}} : i64 to i8

// WIDE-LABEL: llvm.func @solve(
// WIDE: llvm.call @obelisk_rt_v1_random_solve_wide_modes_state
// WIDE: llvm.zext {{.*}} : i64 to i128

//--- narrow-assignment.mlir

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @narrow_assignment {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "narrow_assignment.solve"

    simulation.func private @solve(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %start: i8 {simulation.capture_kind = 2 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %mask = arith.constant -1 : i8
      %constraint_mask = arith.constant 0 : i64
      %limit = arith.constant 8 : i64
      %state = arith.constant 17 : i64
      %increment = arith.constant 3 : i64
      %assignment, %success, %next_state =
          "simulation.random.solve_wide"(
              %ctx, %start, %mask, %constraint_mask, %limit, %state,
              %increment, %capture)
          {program = "v2"} :
          (!simulation.context, i8, i8, i64, i64, i64, i64, i64) ->
          (i8, i1, i64)
      simulation.return %assignment : i8
    }
  }
}

//--- wide-assignment.mlir

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wide_assignment {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "wide_assignment.solve"

    simulation.func private @solve(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %start: i128 {simulation.capture_kind = 2 : i32},
        %capture: i128 {simulation.capture_kind = 2 : i32}) -> i128
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %mask = arith.constant -1 : i128
      %constraint_mask = arith.constant 0 : i64
      %limit = arith.constant 8 : i64
      %state = arith.constant 17 : i64
      %increment = arith.constant 3 : i64
      %assignment, %success, %next_state =
          "simulation.random.solve_wide"(
              %ctx, %start, %mask, %constraint_mask, %limit, %state,
              %increment, %capture)
          {program = "v2"} :
          (!simulation.context, i128, i128, i64, i64, i64, i64, i128) ->
          (i128, i1, i64)
      simulation.return %assignment : i128
    }
  }
}
