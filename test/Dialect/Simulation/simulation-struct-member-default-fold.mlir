// RUN: obelisk-opt %s --canonicalize | FileCheck %s

// IEEE 1800-2023 7.2.2: a member of a struct default holds its initializer,
// and a member without one holds its type's default.

!record = !simulation.unpacked_struct<[#simulation.field<name = "v", type = i32, ordinal = 0, packedOffset = 0, initializer = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32>, #simulation.field<name = "u", type = i32, ordinal = 1, packedOffset = 0>]>

// CHECK-LABEL: func.func @member_default
// CHECK-DAG: %[[SEVEN:.*]] = arith.constant 7 : i32
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-NOT: simulation.aggregate
// CHECK: return %[[SEVEN]], %[[ZERO]]
func.func @member_default() -> (i32, i32) {
  %default = simulation.aggregate.default : !record
  %initialized = simulation.aggregate.extract %default[0] : (!record) -> i32
  %plain = simulation.aggregate.extract %default[1] : (!record) -> i32
  return %initialized, %plain : i32, i32
}
