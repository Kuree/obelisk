// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines --mlir-print-ir-after=prepare-native-managed-roots -o /dev/null 2>&1 | FileCheck %s

// A process needs no managed root scope when it holds only scalar state,
// including fixed unpacked arrays of scalars, and its calls reach only such
// bodies. Recursion stays conservative.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @native_unmanaged_processes {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "unmanaged.increment"
    simulation.code_unit.decl 2 in 0 function hierarchy "unmanaged.recursive"
    simulation.code_unit.decl 3 in 0 initial hierarchy "unmanaged.scalar_caller"
    simulation.code_unit.decl 4 in 0 initial hierarchy "unmanaged.array_holder"
    simulation.code_unit.decl 5 in 0 initial hierarchy "unmanaged.recursive_caller"

    simulation.func private @increment(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i8
      %next = arith.addi %value, %one : i8
      simulation.return %next : i8
    }

    simulation.func private @recursive(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %next = simulation.call @recursive(%ctx, %value) :
          (!simulation.context, i8) -> i8
      simulation.return %next : i8
    }

    simulation.func @scalar_caller(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %result = simulation.call @increment(%ctx, %value) :
          (!simulation.context, i8) -> i8
      simulation.return
    }

    simulation.func @array_holder(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 1 : i32} {
      %array = simulation.aggregate.default :
          !simulation.unpacked_array<0 : 3 x !simulation.logic<8>>
      simulation.return
    }

    simulation.func @recursive_caller(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 1 : i32} {
      %result = simulation.call @recursive(%ctx, %value) :
          (!simulation.context, i8) -> i8
      simulation.return
    }
  }
}

// CHECK: IR Dump After {{.*}}PrepareNativeManagedRootsPass
// CHECK: simulation.func @scalar_caller({{.*}}native.unmanaged
// CHECK: simulation.func @array_holder({{.*}}native.unmanaged
// The NOT range ends at the next line, so it covers only the header.
// CHECK: simulation.func @recursive_caller(
// CHECK-NOT: native.unmanaged
// CHECK-NEXT: simulation.call @recursive(
