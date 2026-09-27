// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @function_types {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "function_types.select"
    simulation.code_unit.decl 2 in 0 initial hierarchy "function_types.caller"

    simulation.func private @select(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32},
        %left: !simulation.logic<8>
            {simulation.capture_kind = 1 : i32},
        %right: !simulation.logic<8>
            {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8>
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %selected = arith.select %condition, %left, %right :
          !simulation.logic<8>
      simulation.return %selected : !simulation.logic<8>
    }

    simulation.func @caller(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %condition = arith.constant true
      %left = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      %right = simulation.logic.constant 2 : i8, -1 : i8 :
          !simulation.logic<8>
      %selected = simulation.call @select(
          %ctx, %condition, %left, %right) :
          (!simulation.context, i1, !simulation.logic<8>,
           !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @select(
// CHECK-SAME: %{{.*}}: !llvm.ptr, %{{.*}}: i1,
// CHECK-SAME: %{{.*}}: i8, %{{.*}}: i8, %{{.*}}: i8, %{{.*}}: i8)
// CHECK-SAME: -> !llvm.struct<(i8, i8)>
// CHECK: llvm.select
// CHECK: llvm.select
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @caller(
// CHECK: llvm.call @select
// CHECK-SAME: (!llvm.ptr, i1, i8, i8, i8, i8) -> !llvm.struct<(i8, i8)>
// CHECK-NOT: simulation.call
// CHECK-NOT: simulation.return
