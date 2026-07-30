// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @function_types {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "function_types.select"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "function_types.caller"

    obelisk_sim.func private @select(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 1 : i32},
        %left: !obelisk_sim.logic<8>
            {obelisk_sim.capture_kind = 1 : i32},
        %right: !obelisk_sim.logic<8>
            {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8>
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %selected = arith.select %condition, %left, %right :
          !obelisk_sim.logic<8>
      obelisk_sim.return %selected : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %condition = arith.constant true
      %left = obelisk_sim.logic.constant 1 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %right = obelisk_sim.logic.constant 2 : i8, -1 : i8 :
          !obelisk_sim.logic<8>
      %selected = obelisk_sim.call @select(
          %ctx, %condition, %left, %right) :
          (!obelisk_sim.context, i1, !obelisk_sim.logic<8>,
           !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
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
// CHECK-NOT: obelisk_sim.call
// CHECK-NOT: obelisk_sim.return
