// RUN: obelisk-opt %s --test-obelisk-integer-interfaces -o /dev/null 2>&1 | FileCheck %s

// Use MLIR's unmodified sparse IntegerRangeAnalysis, not an Obelisk opcode
// switch. Facts from Simulation ops must compose with ordinary arith ops.
func.func @ranges(%input: !simulation.logic<65>, %control: !simulation.logic<1>, %bits: i65, %unpacked: !simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i32, ordinal = 1, packedOffset = 0>]>) -> (i32, i32, i32, i32, i1) {
  %count = simulation.logic.count_bits %input matching %control, %control {test.fact = "count"} : (!simulation.logic<65>, !simulation.logic<1>, !simulation.logic<1>) -> i32
  %bitcount = simulation.logic.count_bits %bits matching %control {test.fact = "bit-count"} : (i65, !simulation.logic<1>) -> i32
  %padded = simulation.logic.count_bits %unpacked matching %control {test.fact = "unpacked-count"} : (!simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i32, ordinal = 1, packedOffset = 0>]>, !simulation.logic<1>) -> i32
  %log = simulation.logic.clog2 %input {test.fact = "log"} : !simulation.logic<65>
  %small = simulation.logic.clog2 %control {test.fact = "one-bit-log"} : !simulation.logic<1>
  %limit = arith.constant 65 : i32
  %within = arith.cmpi ule, %count, %limit {test.fact = "within"} : i32
  return %count, %bitcount, %padded, %log, %within : i32, i32, i32, i32, i1
}
// CHECK: count umin=0 umax=65
// CHECK: bit-count umin=0 umax=65
// CHECK: unpacked-count umin=0 umax=4294967295
// CHECK: log umin=0 umax=65
// CHECK: one-bit-log umin=0 umax=0
// CHECK: within umin=1 umax=1

func.func @casts(%input: i65) -> i65 {
  %logic = simulation.logic.from_bits %input {test.fact = "from-bits"} : i65 -> !simulation.logic<65>
  %wide = simulation.logic.resize %logic signed = true {test.fact = "resize"} : !simulation.logic<65> -> !simulation.logic<128>
  %same = simulation.logic.resize %wide signed = false {test.fact = "identity"} : !simulation.logic<128> -> !simulation.logic<128>
  %narrow = simulation.logic.resize %same signed = false : !simulation.logic<128> -> !simulation.logic<65>
  %bits = simulation.logic.to_bits %narrow {test.fact = "to-bits"} : !simulation.logic<65> -> i65
  return %bits : i65
}
// CHECK: from-bits cast=1
// CHECK: resize cast=1
// CHECK: identity cast=1
// CHECK: to-bits cast=1

func.func @view(%input: !simulation.ref<!simulation.unpacked_array<0 : 15 x !simulation.logic<1>>>, %index: i64) -> !simulation.ref<!simulation.logic<8>> {
  %word = schedule.word.view %input, %index {test.fact = "word-view"} : (!simulation.ref<!simulation.unpacked_array<0 : 15 x !simulation.logic<1>>>, i64) -> !simulation.ref<!simulation.logic<8>>
  return %word : !simulation.ref<!simulation.logic<8>>
}
// CHECK: word-view view=1 dest=1

func.func @packed_casts(%input: !simulation.packed_array<1 : 0 x !simulation.logic<4>>) -> !simulation.packed_array<1 : 0 x !simulation.logic<4>> {
  %flat = simulation.packed.flatten %input {test.fact = "flatten"} : (!simulation.packed_array<1 : 0 x !simulation.logic<4>>) -> !simulation.logic<8>
  %packed = simulation.packed.unflatten %flat {test.fact = "unflatten"} : (!simulation.logic<8>) -> !simulation.packed_array<1 : 0 x !simulation.logic<4>>
  return %packed : !simulation.packed_array<1 : 0 x !simulation.logic<4>>
}
// CHECK: flatten cast=1
// CHECK: unflatten cast=1
