// RUN: obelisk-opt %s --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %s --canonicalize --cse -o %t.once
// RUN: obelisk-opt %t.once --canonicalize --cse -o %t.twice
// RUN: diff %t.once %t.twice

// IEEE 1800-2023 11.4.3-8, 11.6, 11.8: explicit two-state operands
// preserve fixed-width wraparound and signedness. Constants are exposed to
// arith without speculating about unknown planes or exceptional arithmetic.

// CHECK-LABEL: func.func @bits_add(
// CHECK: arith.addi %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_add(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary add %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_sub(
// CHECK: arith.subi %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_sub(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary sub %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_mul(
// CHECK: arith.muli %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_mul(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary mul %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_and(
// CHECK: arith.andi %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_and(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary and %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_or(
// CHECK: arith.ori %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_or(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary or %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_xor(
// CHECK: arith.xori %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_xor(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary xor %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_xnor(
// CHECK: arith.xori %arg0, %arg1 : i8
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @bits_xnor(%a: i8, %b: i8) -> i8 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary xnor %la, %lb : !simulation.logic<8>
  %bits = simulation.logic.to_bits %r : !simulation.logic<8> -> i8
  return %bits : i8
}

// CHECK-LABEL: func.func @bits_eq(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi eq, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_eq(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_ne(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi ne, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_ne(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare ne %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_ult(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi ult, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_ult(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare ult %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_ule(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi ule, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_ule(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare ule %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_ugt(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi ugt, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_ugt(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare ugt %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_uge(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi uge, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_uge(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare uge %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_slt(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi slt, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_slt(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare slt %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_sle(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi sle, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_sle(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare sle %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_sgt(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi sgt, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_sgt(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare sgt %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_sge(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi sge, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_sge(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare sge %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_case_eq(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi eq, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_case_eq(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare case_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @bits_case_ne(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi ne, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_case_ne(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare case_ne %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @bits_casez_eq(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi eq, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_casez_eq(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare casez_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @bits_casexz_eq(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi eq, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_casexz_eq(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare casexz_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @bits_wild_eq(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi eq, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_wild_eq(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare wild_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @bits_wild_ne(
// CHECK-NEXT: %[[R:.*]] = arith.cmpi ne, %arg0, %arg1 : i65
// CHECK-NEXT: return %[[R]] : i1
func.func @bits_wild_ne(%a: i65, %b: i65) -> i1 {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i65 -> !simulation.logic<65>
  %r = simulation.logic.compare wild_ne %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %r : !simulation.logic<1>
  return %truth : i1
}

// The RSD parameter idiom: signed loop induction, known parameter offset,
// then a signed 65-bit array index. Arithmetic stays i32 until the resize.
// CHECK-LABEL: func.func @parameter_index(
// CHECK-DAG: %[[P:.*]] = arith.constant 2 : i32
// CHECK: %[[SUM:.*]] = arith.addi %arg1, %[[P]] : i32
// CHECK: %[[WIDE:.*]] = arith.extsi %[[SUM]] : i32 to i65
// CHECK: %[[REF:.*]] = simulation.ref.array_element %arg0[%[[WIDE]]]
// CHECK-NEXT: return %[[REF]]
func.func @parameter_index(%array: !simulation.ref<!simulation.unpacked_array<-4 : 7 x i8>>, %i: i32) -> !simulation.ref<i8> {
  %iv = simulation.logic.from_bits %i : i32 -> !simulation.logic<32>
  %parameter = simulation.logic.constant 2 : i32, 0 : i32 : !simulation.logic<32>
  %sum = simulation.logic.binary add %iv, %parameter : !simulation.logic<32>
  %wide = simulation.logic.resize %sum signed = true : !simulation.logic<32> -> !simulation.logic<65>
  %element = simulation.ref.array_element %array[%wide] : (!simulation.ref<!simulation.unpacked_array<-4 : 7 x i8>>, !simulation.logic<65>) -> !simulation.ref<i8>
  return %element : !simulation.ref<i8>
}

// A constant on the LHS keeps the correct comparison direction.
// CHECK-LABEL: func.func @parameter_bound(
// CHECK: %[[P:.*]] = arith.constant 8 : i32
// CHECK: %[[CMP:.*]] = arith.cmpi slt, %arg0, %[[P]] : i32
// CHECK-NEXT: return %[[CMP]] : i1
func.func @parameter_bound(%i: i32) -> i1 {
  %iv = simulation.logic.from_bits %i : i32 -> !simulation.logic<32>
  %parameter = simulation.logic.constant 8 : i32, 0 : i32 : !simulation.logic<32>
  %compare = simulation.logic.compare sgt %parameter, %iv : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
  %truth = simulation.logic.is_true %compare : !simulation.logic<1>
  return %truth : i1
}

// Both operands are parameter expressions: ordinary dialect folding suffices.
// CHECK-LABEL: func.func @fold_parameter_expression()
// CHECK-NEXT: %[[P:.*]] = arith.constant 12 : i65
// CHECK-NEXT: return %[[P]] : i65
func.func @fold_parameter_expression() -> i65 {
  %two = simulation.logic.constant 2 : i32, 0 : i32 : !simulation.logic<32>
  %six = simulation.logic.constant 6 : i32, 0 : i32 : !simulation.logic<32>
  %product = simulation.logic.binary mul %two, %six : !simulation.logic<32>
  %wide = simulation.logic.resize %product signed = true : !simulation.logic<32> -> !simulation.logic<65>
  %bits = simulation.logic.to_bits %wide : !simulation.logic<65> -> i65
  return %bits : i65
}

// Truth tests the entire value, including high bits beyond a machine word.
// CHECK-LABEL: func.func @wide_truth(
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i128
// CHECK: %[[R:.*]] = arith.cmpi ne, %arg0, %[[ZERO]] : i128
// CHECK-NEXT: return %[[R]] : i1
func.func @wide_truth(%bits: i128) -> i1 {
  %logic = simulation.logic.from_bits %bits : i128 -> !simulation.logic<128>
  %truth = simulation.logic.is_true %logic : !simulation.logic<128>
  return %truth : i1
}

// CHECK-LABEL: func.func @one_bit_truth(
// CHECK-NEXT: return %arg0 : i1
func.func @one_bit_truth(%bits: i1) -> i1 {
  %logic = simulation.logic.from_bits %bits : i1 -> !simulation.logic<1>
  %truth = simulation.logic.is_true %logic : !simulation.logic<1>
  return %truth : i1
}

// CHECK-LABEL: func.func @sext(
// CHECK-NEXT: %[[R:.*]] = arith.extsi %arg0 : i1 to i65
// CHECK-NEXT: return %[[R]] : i65
func.func @sext(%bits: i1) -> i65 {
  %logic = simulation.logic.from_bits %bits : i1 -> !simulation.logic<1>
  %resized = simulation.logic.resize %logic signed = true : !simulation.logic<1> -> !simulation.logic<65>
  %result = simulation.logic.to_bits %resized : !simulation.logic<65> -> i65
  return %result : i65
}

// CHECK-LABEL: func.func @zext(
// CHECK-NEXT: %[[R:.*]] = arith.extui %arg0 : i8 to i128
// CHECK-NEXT: return %[[R]] : i128
func.func @zext(%bits: i8) -> i128 {
  %logic = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
  %resized = simulation.logic.resize %logic signed = false : !simulation.logic<8> -> !simulation.logic<128>
  %result = simulation.logic.to_bits %resized : !simulation.logic<128> -> i128
  return %result : i128
}

// CHECK-LABEL: func.func @trunc(
// CHECK-NEXT: %[[R:.*]] = arith.trunci %arg0 : i128 to i7
// CHECK-NEXT: return %[[R]] : i7
func.func @trunc(%bits: i128) -> i7 {
  %logic = simulation.logic.from_bits %bits : i128 -> !simulation.logic<128>
  %resized = simulation.logic.resize %logic signed = true : !simulation.logic<128> -> !simulation.logic<7>
  %result = simulation.logic.to_bits %resized : !simulation.logic<7> -> i7
  return %result : i7
}

// CHECK-LABEL: func.func @retain_x(
// Arithmetic propagates a single X/Z operand to the whole result.
// CHECK-DAG: %[[X8:.*]] = simulation.logic.constant 0 : i8, -1 : i8
// CHECK-DAG: %[[X65:.*]] = simulation.logic.constant 0 : i65, -1 : i65
// CHECK: %[[CMP:.*]] = simulation.logic.compare slt
// CHECK-NOT: simulation.logic.binary
// CHECK-NOT: simulation.logic.resize
// CHECK: return %[[X8]], %[[CMP]], %[[X65]]
func.func @retain_x(%bits: i8) -> (!simulation.logic<8>, !simulation.logic<1>, !simulation.logic<65>) {
  %logic = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
  %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
  %sum = simulation.logic.binary add %logic, %unknown : !simulation.logic<8>
  %compare = simulation.logic.compare slt %logic, %unknown : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
  %wide = simulation.logic.resize %sum signed = true : !simulation.logic<8> -> !simulation.logic<65>
  return %sum, %compare, %wide : !simulation.logic<8>, !simulation.logic<1>, !simulation.logic<65>
}

// CHECK-LABEL: func.func @retain_z(
// Arithmetic propagates a single X/Z operand to the whole result.
// CHECK-DAG: %[[X8:.*]] = simulation.logic.constant 0 : i8, -1 : i8
// CHECK-DAG: %[[X65:.*]] = simulation.logic.constant 0 : i65, -1 : i65
// CHECK: %[[CMP:.*]] = simulation.logic.compare slt
// CHECK-NOT: simulation.logic.binary
// CHECK-NOT: simulation.logic.resize
// CHECK: return %[[X8]], %[[CMP]], %[[X65]]
func.func @retain_z(%bits: i8) -> (!simulation.logic<8>, !simulation.logic<1>, !simulation.logic<65>) {
  %logic = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
  %unknown = simulation.logic.constant -1 : i8, -1 : i8 : !simulation.logic<8>
  %sum = simulation.logic.binary add %logic, %unknown : !simulation.logic<8>
  %compare = simulation.logic.compare slt %logic, %unknown : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
  %wide = simulation.logic.resize %sum signed = true : !simulation.logic<8> -> !simulation.logic<65>
  return %sum, %compare, %wide : !simulation.logic<8>, !simulation.logic<1>, !simulation.logic<65>
}

// CHECK-LABEL: func.func @retain_mixed(
// Arithmetic propagates a single X/Z operand to the whole result.
// CHECK-DAG: %[[X8:.*]] = simulation.logic.constant 0 : i8, -1 : i8
// CHECK-DAG: %[[X65:.*]] = simulation.logic.constant 0 : i65, -1 : i65
// CHECK: %[[CMP:.*]] = simulation.logic.compare slt
// CHECK-NOT: simulation.logic.binary
// CHECK-NOT: simulation.logic.resize
// CHECK: return %[[X8]], %[[CMP]], %[[X65]]
func.func @retain_mixed(%bits: i8) -> (!simulation.logic<8>, !simulation.logic<1>, !simulation.logic<65>) {
  %logic = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
  %unknown = simulation.logic.constant 3 : i8, 2 : i8 : !simulation.logic<8>
  %sum = simulation.logic.binary add %logic, %unknown : !simulation.logic<8>
  %compare = simulation.logic.compare slt %logic, %unknown : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
  %wide = simulation.logic.resize %sum signed = true : !simulation.logic<8> -> !simulation.logic<65>
  return %sum, %compare, %wide : !simulation.logic<8>, !simulation.logic<1>, !simulation.logic<65>
}

// A known plane does not prove a nonzero divisor or rule out MIN/-1.
// CHECK-LABEL: func.func @retain_udiv(
// CHECK: simulation.logic.binary udiv
// CHECK: return
func.func @retain_udiv(%a: i8, %b: i8) -> !simulation.logic<8> {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary udiv %la, %lb : !simulation.logic<8>
  return %r : !simulation.logic<8>
}

// A known plane does not prove a nonzero divisor or rule out MIN/-1.
// CHECK-LABEL: func.func @retain_sdiv(
// CHECK: simulation.logic.binary sdiv
// CHECK: return
func.func @retain_sdiv(%a: i8, %b: i8) -> !simulation.logic<8> {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary sdiv %la, %lb : !simulation.logic<8>
  return %r : !simulation.logic<8>
}

// A known plane does not prove a nonzero divisor or rule out MIN/-1.
// CHECK-LABEL: func.func @retain_umod(
// CHECK: simulation.logic.binary umod
// CHECK: return
func.func @retain_umod(%a: i8, %b: i8) -> !simulation.logic<8> {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary umod %la, %lb : !simulation.logic<8>
  return %r : !simulation.logic<8>
}

// A known plane does not prove a nonzero divisor or rule out MIN/-1.
// CHECK-LABEL: func.func @retain_smod(
// CHECK: simulation.logic.binary smod
// CHECK: return
func.func @retain_smod(%a: i8, %b: i8) -> !simulation.logic<8> {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r = simulation.logic.binary smod %la, %lb : !simulation.logic<8>
  return %r : !simulation.logic<8>
}

// Preserve the logical signed index on a descending array, including negative
// indices. Range checks remain owned by the selection operation.
// CHECK-LABEL: func.func @array_read_write(
// CHECK: %[[V:.*]] = simulation.array.extract_dynamic %arg0[%arg1] : (!simulation.unpacked_array<3 : -2 x i8>, i65) -> i8
// CHECK: %[[A:.*]] = simulation.array.insert_dynamic %[[V]] into %arg0[%arg1] : (!simulation.unpacked_array<3 : -2 x i8>, i8, i65)
// CHECK: return %[[A]]
func.func @array_read_write(%array: !simulation.unpacked_array<3 : -2 x i8>, %index: i65) -> !simulation.unpacked_array<3 : -2 x i8> {
  %logic = simulation.logic.from_bits %index : i65 -> !simulation.logic<65>
  %value = simulation.array.extract_dynamic %array[%logic] : (!simulation.unpacked_array<3 : -2 x i8>, !simulation.logic<65>) -> i8
  %updated = simulation.array.insert_dynamic %value into %array[%logic] : (!simulation.unpacked_array<3 : -2 x i8>, i8, !simulation.logic<65>) -> !simulation.unpacked_array<3 : -2 x i8>
  return %updated : !simulation.unpacked_array<3 : -2 x i8>
}

// CHECK-LABEL: func.func @driver_array(
// CHECK-NEXT: %[[D:.*]] = simulation.driver.array_element %arg0[%arg1]
// CHECK-NEXT: return %[[D]]
func.func @driver_array(%driver: !simulation.driver<!simulation.unpacked_array<1 : 0 x !simulation.logic<8>>>, %index: i65) -> !simulation.driver<!simulation.logic<8>> {
  %logic = simulation.logic.from_bits %index : i65 -> !simulation.logic<65>
  %element = simulation.driver.array_element %driver[%logic] : (!simulation.driver<!simulation.unpacked_array<1 : 0 x !simulation.logic<8>>>, !simulation.logic<65>) -> !simulation.driver<!simulation.logic<8>>
  return %element : !simulation.driver<!simulation.logic<8>>
}

// Dynamic packed selections retain partial-overlap and out-of-range behavior.
// CHECK-LABEL: func.func @packed_selections(
// CHECK: simulation.logic.dyn_extract %arg0 from %arg2 : (!simulation.logic<8>, i65)
// CHECK: simulation.logic.dyn_insert %arg1 into %arg0 at %arg2 : (!simulation.logic<8>, !simulation.logic<2>, i65)
// CHECK: return
func.func @packed_selections(%input: !simulation.logic<8>, %replacement: !simulation.logic<2>, %index: i65) -> (!simulation.logic<2>, !simulation.logic<8>) {
  %logic = simulation.logic.from_bits %index : i65 -> !simulation.logic<65>
  %element = simulation.logic.dyn_extract %input from %logic : (!simulation.logic<8>, !simulation.logic<65>) -> !simulation.logic<2>
  %updated = simulation.logic.dyn_insert %replacement into %input at %logic : (!simulation.logic<8>, !simulation.logic<2>, !simulation.logic<65>) -> !simulation.logic<8>
  return %element, %updated : !simulation.logic<2>, !simulation.logic<8>
}

// CHECK-LABEL: func.func @packed_views(
// CHECK: simulation.ref.dyn_extract %arg0 from %arg2 : (!simulation.ref<!simulation.logic<8>>, i65)
// CHECK: simulation.driver.dyn_extract %arg1 from %arg2 : (!simulation.driver<!simulation.logic<8>>, i65)
// CHECK: return
func.func @packed_views(%ref: !simulation.ref<!simulation.logic<8>>, %driver: !simulation.driver<!simulation.logic<8>>, %index: i65) -> (!simulation.ref<!simulation.logic<2>>, !simulation.driver<!simulation.logic<2>>) {
  %logic = simulation.logic.from_bits %index : i65 -> !simulation.logic<65>
  %r = simulation.ref.dyn_extract %ref from %logic : (!simulation.ref<!simulation.logic<8>>, !simulation.logic<65>) -> !simulation.ref<!simulation.logic<2>>
  %d = simulation.driver.dyn_extract %driver from %logic : (!simulation.driver<!simulation.logic<8>>, !simulation.logic<65>) -> !simulation.driver<!simulation.logic<2>>
  return %r, %d : !simulation.ref<!simulation.logic<2>>, !simulation.driver<!simulation.logic<2>>
}
