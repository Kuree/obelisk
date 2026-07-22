// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | mlir-opt --convert-to-llvm \
// RUN:   | mlir-runner -e main -entry-point-result=i32 \
// RUN:   | FileCheck %s --check-prefix=EXEC

// EXEC: 1

module {
  // Exercise the operation variants whose semantics are not covered by the
  // poison/bounds-focused checks in main.
  func.func @exercise_remaining_ops() -> i1 {
    %false = arith.constant false
    %true = arith.constant true
    %c21_i5 = arith.constant 21 : i5

    %zero1 = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
    %one1 = obelisk_sim.logic.constant 1 : i1, 0 : i1 : !obelisk_sim.logic<1>
    %x1 = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
    %z1 = obelisk_sim.logic.constant 1 : i1, 1 : i1 : !obelisk_sim.logic<1>
    %all_x5 = obelisk_sim.logic.constant 0 : i5, -1 : i5 : !obelisk_sim.logic<5>

    // Conversions and every unary operation.
    %from = obelisk_sim.logic.from_bits %c21_i5 : i5 -> !obelisk_sim.logic<5>
    %expected_from = obelisk_sim.logic.constant 21 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %ok_from = obelisk_sim.logic.compare case_eq %from, %expected_from : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %mixed = obelisk_sim.logic.constant 31 : i5, 10 : i5 : !obelisk_sim.logic<5>
    %to = obelisk_sim.logic.to_bits %mixed : !obelisk_sim.logic<5> -> i5
    %ok_to = arith.cmpi eq, %to, %c21_i5 : i5
    %three5 = obelisk_sim.logic.constant 3 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %plus = obelisk_sim.logic.unary plus %three5 : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %neg = obelisk_sim.logic.unary negate %three5 : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %not = obelisk_sim.logic.unary bit_not %mixed : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %logical_not = obelisk_sim.logic.unary logical_not %x1 : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
    %expected_neg = obelisk_sim.logic.constant -3 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %expected_not = obelisk_sim.logic.constant 0 : i5, 10 : i5 : !obelisk_sim.logic<5>
    %ok_plus = obelisk_sim.logic.compare case_eq %plus, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_neg = obelisk_sim.logic.compare case_eq %neg, %expected_neg : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_not = obelisk_sim.logic.compare case_eq %not, %expected_not : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_logical_not = obelisk_sim.logic.compare case_eq %logical_not, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1

    // All reductions, including known-bit domination and unknown parity.
    %red_input = obelisk_sim.logic.constant 21 : i5, 2 : i5 : !obelisk_sim.logic<5>
    %red_and = obelisk_sim.logic.reduction and %red_input : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_nand = obelisk_sim.logic.reduction nand %red_input : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_or = obelisk_sim.logic.reduction or %red_input : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_nor = obelisk_sim.logic.reduction nor %red_input : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_xor = obelisk_sim.logic.reduction xor %red_input : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_xnor = obelisk_sim.logic.reduction xnor %red_input : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %ok_red_and = obelisk_sim.logic.compare case_eq %red_and, %zero1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_red_nand = obelisk_sim.logic.compare case_eq %red_nand, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_red_or = obelisk_sim.logic.compare case_eq %red_or, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_red_nor = obelisk_sim.logic.compare case_eq %red_nor, %zero1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_red_xor = obelisk_sim.logic.compare case_eq %red_xor, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_red_xnor = obelisk_sim.logic.compare case_eq %red_xnor, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1

    // Logical truth tables and known arithmetic variants.
    %logical_and_0 = obelisk_sim.logic.logical and %x1, %zero1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
    %logical_and_x = obelisk_sim.logic.logical and %x1, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
    %logical_or_1 = obelisk_sim.logic.logical or %x1, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
    %logical_or_x = obelisk_sim.logic.logical or %x1, %zero1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
    %ok_land0 = obelisk_sim.logic.compare case_eq %logical_and_0, %zero1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_landx = obelisk_sim.logic.compare case_eq %logical_and_x, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_lor1 = obelisk_sim.logic.compare case_eq %logical_or_1, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_lorx = obelisk_sim.logic.compare case_eq %logical_or_x, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1

    %ten5 = obelisk_sim.logic.constant 10 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %seven5 = obelisk_sim.logic.constant 7 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %thirty5 = obelisk_sim.logic.constant 30 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %one5 = obelisk_sim.logic.constant 1 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %minus_ten5 = obelisk_sim.logic.constant -10 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %minus_three5 = obelisk_sim.logic.constant -3 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %minus_one5 = obelisk_sim.logic.constant -1 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %sub = obelisk_sim.logic.binary sub %ten5, %three5 : !obelisk_sim.logic<5>
    %mul = obelisk_sim.logic.binary mul %ten5, %three5 : !obelisk_sim.logic<5>
    %udiv = obelisk_sim.logic.binary udiv %ten5, %three5 : !obelisk_sim.logic<5>
    %umod = obelisk_sim.logic.binary umod %ten5, %three5 : !obelisk_sim.logic<5>
    %sdiv = obelisk_sim.logic.binary sdiv %minus_ten5, %three5 : !obelisk_sim.logic<5>
    %smod = obelisk_sim.logic.binary smod %minus_ten5, %three5 : !obelisk_sim.logic<5>
    %xnor = obelisk_sim.logic.binary xnor %ten5, %three5 : !obelisk_sim.logic<5>
    %unknown_add = obelisk_sim.logic.binary add %mixed, %red_input : !obelisk_sim.logic<5>
    %expected_xnor = obelisk_sim.logic.constant 22 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %ok_sub = obelisk_sim.logic.compare case_eq %sub, %seven5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_mul = obelisk_sim.logic.compare case_eq %mul, %thirty5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_udiv = obelisk_sim.logic.compare case_eq %udiv, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_umod = obelisk_sim.logic.compare case_eq %umod, %one5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_sdiv = obelisk_sim.logic.compare case_eq %sdiv, %minus_three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_smod = obelisk_sim.logic.compare case_eq %smod, %minus_one5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_xnor = obelisk_sim.logic.compare case_eq %xnor, %expected_xnor : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_unknown_add = obelisk_sim.logic.compare case_eq %unknown_add, %all_x5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1

    // Four-state and ordered comparisons.
    %eq = obelisk_sim.logic.compare eq %ten5, %ten5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ne = obelisk_sim.logic.compare ne %ten5, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ult = obelisk_sim.logic.compare ult %three5, %ten5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ule = obelisk_sim.logic.compare ule %three5, %ten5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ugt = obelisk_sim.logic.compare ugt %ten5, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %uge = obelisk_sim.logic.compare uge %ten5, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %slt = obelisk_sim.logic.compare slt %minus_ten5, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %sle = obelisk_sim.logic.compare sle %minus_ten5, %three5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %sgt = obelisk_sim.logic.compare sgt %three5, %minus_ten5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %sge = obelisk_sim.logic.compare sge %three5, %minus_ten5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %unknown_eq = obelisk_sim.logic.compare eq %mixed, %mixed : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %case_xz = obelisk_sim.logic.compare case_eq %x1, %z1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %case_ne_xz = obelisk_sim.logic.compare case_ne %x1, %z1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_eq = obelisk_sim.logic.compare case_eq %eq, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_ne = obelisk_sim.logic.compare case_eq %ne, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_ult = obelisk_sim.logic.compare case_eq %ult, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_ule = obelisk_sim.logic.compare case_eq %ule, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_ugt = obelisk_sim.logic.compare case_eq %ugt, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_uge = obelisk_sim.logic.compare case_eq %uge, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_slt = obelisk_sim.logic.compare case_eq %slt, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_sle = obelisk_sim.logic.compare case_eq %sle, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_sgt = obelisk_sim.logic.compare case_eq %sgt, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_sge = obelisk_sim.logic.compare case_eq %sge, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_unknown_eq = obelisk_sim.logic.compare case_eq %unknown_eq, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_case_xz = arith.cmpi eq, %case_xz, %false : i1
    %ok_case_ne_xz = arith.cmpi eq, %case_ne_xz, %true : i1

    // Copying operations preserve exact X/Z planes.
    %a2 = obelisk_sim.logic.constant 2 : i2, 1 : i2 : !obelisk_sim.logic<2>
    %resize_u = obelisk_sim.logic.resize %a2 signed = false : !obelisk_sim.logic<2> -> !obelisk_sim.logic<5>
    %expected_resize_u = obelisk_sim.logic.constant 2 : i5, 1 : i5 : !obelisk_sim.logic<5>
    %concat = obelisk_sim.logic.concat %a2, %z1 : (!obelisk_sim.logic<2>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<3>
    %expected_concat = obelisk_sim.logic.constant 5 : i3, 3 : i3 : !obelisk_sim.logic<3>
    %replicate = obelisk_sim.logic.replicate %a2 times 3 : !obelisk_sim.logic<2> -> !obelisk_sim.logic<6>
    %expected_replicate = obelisk_sim.logic.constant 42 : i6, 21 : i6 : !obelisk_sim.logic<6>
    %source5 = obelisk_sim.logic.constant 21 : i5, 4 : i5 : !obelisk_sim.logic<5>
    %extract = obelisk_sim.logic.extract %source5 from 1 : !obelisk_sim.logic<5> -> !obelisk_sim.logic<3>
    %expected_extract = obelisk_sim.logic.constant 2 : i3, 2 : i3 : !obelisk_sim.logic<3>
    %base5 = obelisk_sim.logic.constant 0 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %insert = obelisk_sim.logic.insert %z1 into %base5 at 2 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<5>
    %expected_insert = obelisk_sim.logic.constant 4 : i5, 4 : i5 : !obelisk_sim.logic<5>
    %shift_amount = obelisk_sim.logic.constant 1 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %shifted_left = obelisk_sim.logic.shift left %three5 by %shift_amount : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %shifted = obelisk_sim.logic.shift right %mixed by %shift_amount : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %shifted_arith = obelisk_sim.logic.shift right_arith %minus_ten5 by %shift_amount : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %expected_shifted_left = obelisk_sim.logic.constant 6 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %expected_shifted = obelisk_sim.logic.constant 15 : i5, 5 : i5 : !obelisk_sim.logic<5>
    %expected_shifted_arith = obelisk_sim.logic.constant -5 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %ok_resize_u = obelisk_sim.logic.compare case_eq %resize_u, %expected_resize_u : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_concat = obelisk_sim.logic.compare case_eq %concat, %expected_concat : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1
    %ok_replicate = obelisk_sim.logic.compare case_eq %replicate, %expected_replicate : (!obelisk_sim.logic<6>, !obelisk_sim.logic<6>) -> i1
    %ok_extract = obelisk_sim.logic.compare case_eq %extract, %expected_extract : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1
    %ok_insert = obelisk_sim.logic.compare case_eq %insert, %expected_insert : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_shifted_left = obelisk_sim.logic.compare case_eq %shifted_left, %expected_shifted_left : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_shifted = obelisk_sim.logic.compare case_eq %shifted, %expected_shifted : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_shifted_arith = obelisk_sim.logic.compare case_eq %shifted_arith, %expected_shifted_arith : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1

    %ok0 = arith.andi %ok_from, %ok_to : i1
    %ok1 = arith.andi %ok0, %ok_plus : i1
    %ok2 = arith.andi %ok1, %ok_neg : i1
    %ok3 = arith.andi %ok2, %ok_not : i1
    %ok4 = arith.andi %ok3, %ok_logical_not : i1
    %ok5 = arith.andi %ok4, %ok_red_and : i1
    %ok6 = arith.andi %ok5, %ok_red_nand : i1
    %ok7 = arith.andi %ok6, %ok_red_or : i1
    %ok8 = arith.andi %ok7, %ok_red_nor : i1
    %ok9 = arith.andi %ok8, %ok_red_xor : i1
    %ok10 = arith.andi %ok9, %ok_red_xnor : i1
    %ok11 = arith.andi %ok10, %ok_land0 : i1
    %ok12 = arith.andi %ok11, %ok_landx : i1
    %ok13 = arith.andi %ok12, %ok_lor1 : i1
    %ok14 = arith.andi %ok13, %ok_lorx : i1
    %ok15 = arith.andi %ok14, %ok_sub : i1
    %ok16 = arith.andi %ok15, %ok_mul : i1
    %ok17 = arith.andi %ok16, %ok_udiv : i1
    %ok18 = arith.andi %ok17, %ok_umod : i1
    %ok19 = arith.andi %ok18, %ok_sdiv : i1
    %ok20 = arith.andi %ok19, %ok_smod : i1
    %ok21 = arith.andi %ok20, %ok_xnor : i1
    %ok22 = arith.andi %ok21, %ok_unknown_add : i1
    %ok23 = arith.andi %ok22, %ok_eq : i1
    %ok24 = arith.andi %ok23, %ok_ne : i1
    %ok25 = arith.andi %ok24, %ok_ult : i1
    %ok26 = arith.andi %ok25, %ok_ule : i1
    %ok27 = arith.andi %ok26, %ok_ugt : i1
    %ok28 = arith.andi %ok27, %ok_uge : i1
    %ok29 = arith.andi %ok28, %ok_slt : i1
    %ok30 = arith.andi %ok29, %ok_sle : i1
    %ok31 = arith.andi %ok30, %ok_sgt : i1
    %ok32 = arith.andi %ok31, %ok_sge : i1
    %ok33 = arith.andi %ok32, %ok_unknown_eq : i1
    %ok34 = arith.andi %ok33, %ok_case_xz : i1
    %ok35 = arith.andi %ok34, %ok_case_ne_xz : i1
    %ok36 = arith.andi %ok35, %ok_resize_u : i1
    %ok37 = arith.andi %ok36, %ok_concat : i1
    %ok38 = arith.andi %ok37, %ok_replicate : i1
    %ok39 = arith.andi %ok38, %ok_extract : i1
    %ok40 = arith.andi %ok39, %ok_insert : i1
    %ok41 = arith.andi %ok40, %ok_shifted_left : i1
    %ok42 = arith.andi %ok41, %ok_shifted : i1
    %ok43 = arith.andi %ok42, %ok_shifted_arith : i1
    return %ok43 : i1
  }

  func.func @main() -> i32 attributes {llvm.emit_c_interface} {
    %false = arith.constant false
    %true = arith.constant true

    // Scalar 0/1/X/Z control truth.
    %zero1 = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
    %one1 = obelisk_sim.logic.constant 1 : i1, 0 : i1 : !obelisk_sim.logic<1>
    %x1 = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
    %z1 = obelisk_sim.logic.constant 1 : i1, 1 : i1 : !obelisk_sim.logic<1>
    %truth0 = obelisk_sim.logic.is_true %zero1 : !obelisk_sim.logic<1>
    %truth1 = obelisk_sim.logic.is_true %one1 : !obelisk_sim.logic<1>
    %truthx = obelisk_sim.logic.is_true %x1 : !obelisk_sim.logic<1>
    %truthz = obelisk_sim.logic.is_true %z1 : !obelisk_sim.logic<1>
    %ok_t0 = arith.cmpi eq, %truth0, %false : i1
    %ok_t1 = arith.cmpi eq, %truth1, %true : i1
    %ok_tx = arith.cmpi eq, %truthx, %false : i1
    %ok_tz = arith.cmpi eq, %truthz, %false : i1
    %ok_t01 = arith.andi %ok_t0, %ok_t1 : i1
    %ok_txz = arith.andi %ok_tx, %ok_tz : i1
    %ok_truth = arith.andi %ok_t01, %ok_txz : i1

    // Dominating known bits and newly generated X values.
    %x_and_zero = obelisk_sim.logic.binary and %x1, %zero1 : !obelisk_sim.logic<1>
    %x_or_one = obelisk_sim.logic.binary or %x1, %one1 : !obelisk_sim.logic<1>
    %x_xor_one = obelisk_sim.logic.binary xor %x1, %one1 : !obelisk_sim.logic<1>
    %ok_and = obelisk_sim.logic.compare case_eq %x_and_zero, %zero1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_or = obelisk_sim.logic.compare case_eq %x_or_one, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_xor = obelisk_sim.logic.compare case_eq %x_xor_one, %x1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
    %ok_bit_0 = arith.andi %ok_and, %ok_or : i1
    %ok_bit = arith.andi %ok_bit_0, %ok_xor : i1

    // Width 5 signed extension copies the exact sign state, including Z.
    %sign_z5 = obelisk_sim.logic.constant 16 : i5, 16 : i5 : !obelisk_sim.logic<5>
    %sign_z37 = obelisk_sim.logic.resize %sign_z5 signed = true : !obelisk_sim.logic<5> -> !obelisk_sim.logic<37>
    %expected_z37 = obelisk_sim.logic.constant 137438953456 : i37, 137438953456 : i37 : !obelisk_sim.logic<37>
    %ok_resize = obelisk_sim.logic.compare case_eq %sign_z37, %expected_z37 : (!obelisk_sim.logic<37>, !obelisk_sim.logic<37>) -> i1

    // Width 37 known arithmetic.
    %a37 = obelisk_sim.logic.constant 68719476731 : i37, 0 : i37 : !obelisk_sim.logic<37>
    %b37 = obelisk_sim.logic.constant 7 : i37, 0 : i37 : !obelisk_sim.logic<37>
    %sum37 = obelisk_sim.logic.binary add %a37, %b37 : !obelisk_sim.logic<37>
    %expected_sum37 = obelisk_sim.logic.constant 68719476738 : i37, 0 : i37 : !obelisk_sim.logic<37>
    %ok_sum37 = obelisk_sim.logic.compare case_eq %sum37, %expected_sum37 : (!obelisk_sim.logic<37>, !obelisk_sim.logic<37>) -> i1

    // Width 65 known arithmetic and poison-free signed overflow.
    %two65 = obelisk_sim.logic.constant 2 : i65, 0 : i65 : !obelisk_sim.logic<65>
    %three65 = obelisk_sim.logic.constant 3 : i65, 0 : i65 : !obelisk_sim.logic<65>
    %five65 = obelisk_sim.logic.constant 5 : i65, 0 : i65 : !obelisk_sim.logic<65>
    %sum65 = obelisk_sim.logic.binary add %two65, %three65 : !obelisk_sim.logic<65>
    %ok_sum65 = obelisk_sim.logic.compare case_eq %sum65, %five65 : (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> i1
    %min65 = obelisk_sim.logic.constant 18446744073709551616 : i65, 0 : i65 : !obelisk_sim.logic<65>
    %minus_one65 = obelisk_sim.logic.constant -1 : i65, 0 : i65 : !obelisk_sim.logic<65>
    %overflow_div = obelisk_sim.logic.binary sdiv %min65, %minus_one65 : !obelisk_sim.logic<65>
    %overflow_mod = obelisk_sim.logic.binary smod %min65, %minus_one65 : !obelisk_sim.logic<65>
    %zero65 = obelisk_sim.logic.constant 0 : i65, 0 : i65 : !obelisk_sim.logic<65>
    %ok_overflow_div = obelisk_sim.logic.compare case_eq %overflow_div, %min65 : (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> i1
    %ok_overflow_mod = obelisk_sim.logic.compare case_eq %overflow_mod, %zero65 : (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> i1

    // Division and modulo by zero produce canonical all-X results.
    %ten5 = obelisk_sim.logic.constant 10 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %zero5 = obelisk_sim.logic.constant 0 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %all_x5 = obelisk_sim.logic.constant 0 : i5, -1 : i5 : !obelisk_sim.logic<5>
    %div_zero = obelisk_sim.logic.binary udiv %ten5, %zero5 : !obelisk_sim.logic<5>
    %mod_zero = obelisk_sim.logic.binary umod %ten5, %zero5 : !obelisk_sim.logic<5>
    %ok_div_zero = obelisk_sim.logic.compare case_eq %div_zero, %all_x5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_mod_zero = obelisk_sim.logic.compare case_eq %mod_zero, %all_x5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1

    %minus_two5 = obelisk_sim.logic.constant -2 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %signed_lt = obelisk_sim.logic.compare slt %minus_two5, %ten5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ok_signed_cmp = obelisk_sim.logic.compare case_eq %signed_lt, %one1 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1

    // Unknown and oversized shifts never feed an invalid amount to arith.
    %value5 = obelisk_sim.logic.constant 17 : i5, 4 : i5 : !obelisk_sim.logic<5>
    %unknown_amount = obelisk_sim.logic.constant 1 : i5, 1 : i5 : !obelisk_sim.logic<5>
    %large_amount = obelisk_sim.logic.constant 7 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %unknown_shift = obelisk_sim.logic.shift left %value5 by %unknown_amount : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %large_shift = obelisk_sim.logic.shift left %value5 by %large_amount : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %large_ashr = obelisk_sim.logic.shift right_arith %sign_z5 by %large_amount : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %all_z5 = obelisk_sim.logic.constant -1 : i5, -1 : i5 : !obelisk_sim.logic<5>
    %ok_unknown_shift = obelisk_sim.logic.compare case_eq %unknown_shift, %all_x5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_large_shift = obelisk_sim.logic.compare case_eq %large_shift, %zero5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ok_large_ashr = obelisk_sim.logic.compare case_eq %large_ashr, %all_z5 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1

    // Dynamic selections: valid, negative overlap, high overlap, fully out of
    // range, and unknown. Four-state invalid bits are X; two-state bits are 0.
    %idx1 = obelisk_sim.logic.constant 1 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %idx_neg1 = obelisk_sim.logic.constant -1 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %idx4 = obelisk_sim.logic.constant 4 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %idx6 = obelisk_sim.logic.constant 6 : i5, 0 : i5 : !obelisk_sim.logic<5>
    %idx_unknown = obelisk_sim.logic.constant 0 : i5, 1 : i5 : !obelisk_sim.logic<5>
    %sel1 = obelisk_sim.logic.dyn_extract %value5 from %idx1 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<3>
    %sel_neg1 = obelisk_sim.logic.dyn_extract %value5 from %idx_neg1 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<3>
    %sel4 = obelisk_sim.logic.dyn_extract %value5 from %idx4 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<3>
    %sel6 = obelisk_sim.logic.dyn_extract %value5 from %idx6 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<3>
    %sel_unknown = obelisk_sim.logic.dyn_extract %value5 from %idx_unknown : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<3>
    %expected_sel1 = obelisk_sim.logic.constant 0 : i3, 2 : i3 : !obelisk_sim.logic<3>
    %expected_sel_neg1 = obelisk_sim.logic.constant 2 : i3, 1 : i3 : !obelisk_sim.logic<3>
    %expected_sel4 = obelisk_sim.logic.constant 1 : i3, 6 : i3 : !obelisk_sim.logic<3>
    %all_x3 = obelisk_sim.logic.constant 0 : i3, 7 : i3 : !obelisk_sim.logic<3>
    %ok_sel1 = obelisk_sim.logic.compare case_eq %sel1, %expected_sel1 : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1
    %ok_sel_neg1 = obelisk_sim.logic.compare case_eq %sel_neg1, %expected_sel_neg1 : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1
    %ok_sel4 = obelisk_sim.logic.compare case_eq %sel4, %expected_sel4 : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1
    %ok_sel6 = obelisk_sim.logic.compare case_eq %sel6, %all_x3 : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1
    %ok_sel_unknown = obelisk_sim.logic.compare case_eq %sel_unknown, %all_x3 : (!obelisk_sim.logic<3>, !obelisk_sim.logic<3>) -> i1

    %bits5 = arith.constant 21 : i5
    %bits_neg1 = obelisk_sim.bits.dyn_extract %bits5 from %idx_neg1 : (i5, !obelisk_sim.logic<5>) -> i3
    %bits4 = obelisk_sim.bits.dyn_extract %bits5 from %idx4 : (i5, !obelisk_sim.logic<5>) -> i3
    %bits6 = obelisk_sim.bits.dyn_extract %bits5 from %idx6 : (i5, !obelisk_sim.logic<5>) -> i3
    %bits_unknown = obelisk_sim.bits.dyn_extract %bits5 from %idx_unknown : (i5, !obelisk_sim.logic<5>) -> i3
    %c2_i3 = arith.constant 2 : i3
    %c1_i3 = arith.constant 1 : i3
    %c0_i3 = arith.constant 0 : i3
    %ok_bits_neg1 = arith.cmpi eq, %bits_neg1, %c2_i3 : i3
    %ok_bits4 = arith.cmpi eq, %bits4, %c1_i3 : i3
    %ok_bits6 = arith.cmpi eq, %bits6, %c0_i3 : i3
    %ok_bits_unknown = arith.cmpi eq, %bits_unknown, %c0_i3 : i3

    // An unsigned source index is normalized by zero-extension before the
    // signed two's-complement dynamic-index contract is applied.
    %wide_bits37 = arith.constant 2147483648 : i37
    %idx31_i6 = arith.constant 31 : i6
    %bit31 = obelisk_sim.bits.dyn_extract %wide_bits37 from %idx31_i6 : (i37, i6) -> i1
    %ok_unsigned_index = arith.cmpi eq, %bit31, %true : i1

    %ok0 = arith.andi %ok_truth, %ok_bit : i1
    %ok1 = arith.andi %ok0, %ok_resize : i1
    %ok2 = arith.andi %ok1, %ok_sum37 : i1
    %ok3 = arith.andi %ok2, %ok_sum65 : i1
    %ok4 = arith.andi %ok3, %ok_overflow_div : i1
    %ok5 = arith.andi %ok4, %ok_overflow_mod : i1
    %ok6 = arith.andi %ok5, %ok_div_zero : i1
    %ok7 = arith.andi %ok6, %ok_mod_zero : i1
    %ok8 = arith.andi %ok7, %ok_signed_cmp : i1
    %ok9 = arith.andi %ok8, %ok_unknown_shift : i1
    %ok10 = arith.andi %ok9, %ok_large_shift : i1
    %ok11 = arith.andi %ok10, %ok_large_ashr : i1
    %ok12 = arith.andi %ok11, %ok_sel1 : i1
    %ok13 = arith.andi %ok12, %ok_sel_neg1 : i1
    %ok14 = arith.andi %ok13, %ok_sel4 : i1
    %ok15 = arith.andi %ok14, %ok_sel6 : i1
    %ok16 = arith.andi %ok15, %ok_sel_unknown : i1
    %ok17 = arith.andi %ok16, %ok_bits_neg1 : i1
    %ok18 = arith.andi %ok17, %ok_bits4 : i1
    %ok19 = arith.andi %ok18, %ok_bits6 : i1
    %ok20 = arith.andi %ok19, %ok_bits_unknown : i1
    %ok_selection = arith.andi %ok20, %ok_unsigned_index : i1
    %remaining = func.call @exercise_remaining_ops() : () -> i1
    %ok21 = arith.andi %ok_selection, %remaining : i1
    %result = arith.extui %ok21 : i1 to i32
    return %result : i32
  }
}
