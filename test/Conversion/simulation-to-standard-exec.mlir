// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard --canonicalize --cse | FileCheck %s

// Every semantic assertion below is constant. Check that lowering and folding
// prove all of them, without translating to LLVM or executing a JIT program.
// CHECK-LABEL: func.func @exercise_remaining_ops() -> i1
// CHECK-NEXT: %[[TRUE:.*]] = arith.constant true
// CHECK-NEXT: return %[[TRUE]] : i1
// CHECK-NEXT: }
// CHECK-LABEL: func.func @main() -> i32
// CHECK-NEXT: %[[OK:.*]] = call @exercise_remaining_ops() : () -> i1
// CHECK-NEXT: %[[RESULT:.*]] = arith.extui %[[OK]] : i1 to i32
// CHECK-NEXT: return %[[RESULT]] : i32
// CHECK-NEXT: }

module {
  // Exercise the operation variants whose semantics are not covered by the
  // poison/bounds-focused checks in main.
  func.func @exercise_remaining_ops() -> i1 {
    %false = arith.constant false
    %true = arith.constant true
    %c21_i5 = arith.constant 21 : i5

    %zero1 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
    %one1 = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
    %x1 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
    %z1 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
    %all_x5 = simulation.logic.constant 0 : i5, -1 : i5 : !simulation.logic<5>

    // Conversions and every unary operation.
    %from = simulation.logic.from_bits %c21_i5 : i5 -> !simulation.logic<5>
    %expected_from = simulation.logic.constant 21 : i5, 0 : i5 : !simulation.logic<5>
    %ok_from = simulation.logic.compare case_eq %from, %expected_from : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %mixed = simulation.logic.constant 31 : i5, 10 : i5 : !simulation.logic<5>
    %to = simulation.logic.to_bits %mixed : !simulation.logic<5> -> i5
    %ok_to = arith.cmpi eq, %to, %c21_i5 : i5
    %three5 = simulation.logic.constant 3 : i5, 0 : i5 : !simulation.logic<5>
    %plus = simulation.logic.unary plus %three5 : (!simulation.logic<5>) -> !simulation.logic<5>
    %neg = simulation.logic.unary negate %three5 : (!simulation.logic<5>) -> !simulation.logic<5>
    %not = simulation.logic.unary bit_not %mixed : (!simulation.logic<5>) -> !simulation.logic<5>
    %logical_not = simulation.logic.unary logical_not %x1 : (!simulation.logic<1>) -> !simulation.logic<1>
    %expected_neg = simulation.logic.constant -3 : i5, 0 : i5 : !simulation.logic<5>
    %expected_not = simulation.logic.constant 0 : i5, 10 : i5 : !simulation.logic<5>
    %ok_plus = simulation.logic.compare case_eq %plus, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_neg = simulation.logic.compare case_eq %neg, %expected_neg : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_not = simulation.logic.compare case_eq %not, %expected_not : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_logical_not = simulation.logic.compare case_eq %logical_not, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1

    // All reductions, including known-bit domination and unknown parity.
    %red_input = simulation.logic.constant 21 : i5, 2 : i5 : !simulation.logic<5>
    %red_and = simulation.logic.reduction and %red_input : !simulation.logic<5> -> !simulation.logic<1>
    %red_nand = simulation.logic.reduction nand %red_input : !simulation.logic<5> -> !simulation.logic<1>
    %red_or = simulation.logic.reduction or %red_input : !simulation.logic<5> -> !simulation.logic<1>
    %red_nor = simulation.logic.reduction nor %red_input : !simulation.logic<5> -> !simulation.logic<1>
    %red_xor = simulation.logic.reduction xor %red_input : !simulation.logic<5> -> !simulation.logic<1>
    %red_xnor = simulation.logic.reduction xnor %red_input : !simulation.logic<5> -> !simulation.logic<1>
    %ok_red_and = simulation.logic.compare case_eq %red_and, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_red_nand = simulation.logic.compare case_eq %red_nand, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_red_or = simulation.logic.compare case_eq %red_or, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_red_nor = simulation.logic.compare case_eq %red_nor, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_red_xor = simulation.logic.compare case_eq %red_xor, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_red_xnor = simulation.logic.compare case_eq %red_xnor, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1

    // Logical truth tables and known arithmetic variants.
    %logical_and_0 = simulation.logic.logical and %x1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %logical_and_x = simulation.logic.logical and %x1, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %logical_or_1 = simulation.logic.logical or %x1, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %logical_or_x = simulation.logic.logical or %x1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %ok_land0 = simulation.logic.compare case_eq %logical_and_0, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_landx = simulation.logic.compare case_eq %logical_and_x, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_lor1 = simulation.logic.compare case_eq %logical_or_1, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_lorx = simulation.logic.compare case_eq %logical_or_x, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1

    %ten5 = simulation.logic.constant 10 : i5, 0 : i5 : !simulation.logic<5>
    %seven5 = simulation.logic.constant 7 : i5, 0 : i5 : !simulation.logic<5>
    %thirty5 = simulation.logic.constant 30 : i5, 0 : i5 : !simulation.logic<5>
    %one5 = simulation.logic.constant 1 : i5, 0 : i5 : !simulation.logic<5>
    %minus_ten5 = simulation.logic.constant -10 : i5, 0 : i5 : !simulation.logic<5>
    %minus_three5 = simulation.logic.constant -3 : i5, 0 : i5 : !simulation.logic<5>
    %minus_one5 = simulation.logic.constant -1 : i5, 0 : i5 : !simulation.logic<5>
    %sub = simulation.logic.binary sub %ten5, %three5 : !simulation.logic<5>
    %mul = simulation.logic.binary mul %ten5, %three5 : !simulation.logic<5>
    %udiv = simulation.logic.binary udiv %ten5, %three5 : !simulation.logic<5>
    %umod = simulation.logic.binary umod %ten5, %three5 : !simulation.logic<5>
    %sdiv = simulation.logic.binary sdiv %minus_ten5, %three5 : !simulation.logic<5>
    %smod = simulation.logic.binary smod %minus_ten5, %three5 : !simulation.logic<5>
    %xnor = simulation.logic.binary xnor %ten5, %three5 : !simulation.logic<5>
    %unknown_add = simulation.logic.binary add %mixed, %red_input : !simulation.logic<5>
    %expected_xnor = simulation.logic.constant 22 : i5, 0 : i5 : !simulation.logic<5>
    %ok_sub = simulation.logic.compare case_eq %sub, %seven5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_mul = simulation.logic.compare case_eq %mul, %thirty5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_udiv = simulation.logic.compare case_eq %udiv, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_umod = simulation.logic.compare case_eq %umod, %one5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_sdiv = simulation.logic.compare case_eq %sdiv, %minus_three5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_smod = simulation.logic.compare case_eq %smod, %minus_one5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_xnor = simulation.logic.compare case_eq %xnor, %expected_xnor : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_unknown_add = simulation.logic.compare case_eq %unknown_add, %all_x5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1

    // Four-state and ordered comparisons.
    %eq = simulation.logic.compare eq %ten5, %ten5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ne = simulation.logic.compare ne %ten5, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ult = simulation.logic.compare ult %three5, %ten5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ule = simulation.logic.compare ule %three5, %ten5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ugt = simulation.logic.compare ugt %ten5, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %uge = simulation.logic.compare uge %ten5, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %slt = simulation.logic.compare slt %minus_ten5, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %sle = simulation.logic.compare sle %minus_ten5, %three5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %sgt = simulation.logic.compare sgt %three5, %minus_ten5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %sge = simulation.logic.compare sge %three5, %minus_ten5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %unknown_eq = simulation.logic.compare eq %mixed, %mixed : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %case_xz = simulation.logic.compare case_eq %x1, %z1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %case_ne_xz = simulation.logic.compare case_ne %x1, %z1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_eq = simulation.logic.compare case_eq %eq, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_ne = simulation.logic.compare case_eq %ne, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_ult = simulation.logic.compare case_eq %ult, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_ule = simulation.logic.compare case_eq %ule, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_ugt = simulation.logic.compare case_eq %ugt, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_uge = simulation.logic.compare case_eq %uge, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_slt = simulation.logic.compare case_eq %slt, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_sle = simulation.logic.compare case_eq %sle, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_sgt = simulation.logic.compare case_eq %sgt, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_sge = simulation.logic.compare case_eq %sge, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_unknown_eq = simulation.logic.compare case_eq %unknown_eq, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_case_xz = arith.cmpi eq, %case_xz, %false : i1
    %ok_case_ne_xz = arith.cmpi eq, %case_ne_xz, %true : i1
    %wild_mask_x = simulation.logic.compare wild_eq %x1, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %wild_lhs_x = simulation.logic.compare wild_eq %x1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %wild_mismatch = simulation.logic.compare wild_eq %one1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %wild_ne_x = simulation.logic.compare wild_ne %x1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %casez_z0 = simulation.logic.compare casez_eq %z1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %casez_x0 = simulation.logic.compare casez_eq %x1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %casez_xx = simulation.logic.compare casez_eq %x1, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %casex_x0 = simulation.logic.compare casexz_eq %x1, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %casex_z1 = simulation.logic.compare casexz_eq %z1, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_wild_mask_x = simulation.logic.compare case_eq %wild_mask_x, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_wild_lhs_x = simulation.logic.compare case_eq %wild_lhs_x, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_wild_mismatch = simulation.logic.compare case_eq %wild_mismatch, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_wild_ne_x = simulation.logic.compare case_eq %wild_ne_x, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_casez_z0 = arith.cmpi eq, %casez_z0, %true : i1
    %ok_casez_x0 = arith.cmpi eq, %casez_x0, %false : i1
    %ok_casez_xx = arith.cmpi eq, %casez_xx, %true : i1
    %ok_casex_x0 = arith.cmpi eq, %casex_x0, %true : i1
    %ok_casex_z1 = arith.cmpi eq, %casex_z1, %true : i1
    %matching0 = arith.andi %ok_wild_mask_x, %ok_wild_lhs_x : i1
    %matching1 = arith.andi %matching0, %ok_wild_mismatch : i1
    %matching2 = arith.andi %matching1, %ok_wild_ne_x : i1
    %matching3 = arith.andi %matching2, %ok_casez_z0 : i1
    %matching4 = arith.andi %matching3, %ok_casez_x0 : i1
    %matching5 = arith.andi %matching4, %ok_casez_xx : i1
    %matching6 = arith.andi %matching5, %ok_casex_x0 : i1
    %ok_matching = arith.andi %matching6, %ok_casex_z1 : i1

    // Copying operations preserve exact X/Z planes.
    %a2 = simulation.logic.constant 2 : i2, 1 : i2 : !simulation.logic<2>
    %resize_u = simulation.logic.resize %a2 signed = false : !simulation.logic<2> -> !simulation.logic<5>
    %expected_resize_u = simulation.logic.constant 2 : i5, 1 : i5 : !simulation.logic<5>
    %concat = simulation.logic.concat %a2, %z1 : (!simulation.logic<2>, !simulation.logic<1>) -> !simulation.logic<3>
    %expected_concat = simulation.logic.constant 5 : i3, 3 : i3 : !simulation.logic<3>
    %replicate = simulation.logic.replicate %a2 times 3 : !simulation.logic<2> -> !simulation.logic<6>
    %expected_replicate = simulation.logic.constant 42 : i6, 21 : i6 : !simulation.logic<6>
    %source5 = simulation.logic.constant 21 : i5, 4 : i5 : !simulation.logic<5>
    %extract = simulation.logic.extract %source5 from 1 : !simulation.logic<5> -> !simulation.logic<3>
    %expected_extract = simulation.logic.constant 2 : i3, 2 : i3 : !simulation.logic<3>
    %base5 = simulation.logic.constant 0 : i5, 0 : i5 : !simulation.logic<5>
    %insert = simulation.logic.insert %z1 into %base5 at 2 : (!simulation.logic<5>, !simulation.logic<1>) -> !simulation.logic<5>
    %expected_insert = simulation.logic.constant 4 : i5, 4 : i5 : !simulation.logic<5>
    %shift_amount = simulation.logic.constant 1 : i5, 0 : i5 : !simulation.logic<5>
    %shifted_left = simulation.logic.shift left %three5 by %shift_amount : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %shifted = simulation.logic.shift right %mixed by %shift_amount : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %shifted_arith = simulation.logic.shift right_arith %minus_ten5 by %shift_amount : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %expected_shifted_left = simulation.logic.constant 6 : i5, 0 : i5 : !simulation.logic<5>
    %expected_shifted = simulation.logic.constant 15 : i5, 5 : i5 : !simulation.logic<5>
    %expected_shifted_arith = simulation.logic.constant -5 : i5, 0 : i5 : !simulation.logic<5>
    %ok_resize_u = simulation.logic.compare case_eq %resize_u, %expected_resize_u : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_concat = simulation.logic.compare case_eq %concat, %expected_concat : (!simulation.logic<3>, !simulation.logic<3>) -> i1
    %ok_replicate = simulation.logic.compare case_eq %replicate, %expected_replicate : (!simulation.logic<6>, !simulation.logic<6>) -> i1
    %ok_extract = simulation.logic.compare case_eq %extract, %expected_extract : (!simulation.logic<3>, !simulation.logic<3>) -> i1
    %ok_insert = simulation.logic.compare case_eq %insert, %expected_insert : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_shifted_left = simulation.logic.compare case_eq %shifted_left, %expected_shifted_left : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_shifted = simulation.logic.compare case_eq %shifted, %expected_shifted : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_shifted_arith = simulation.logic.compare case_eq %shifted_arith, %expected_shifted_arith : (!simulation.logic<5>, !simulation.logic<5>) -> i1

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
    %ok36 = arith.andi %ok35, %ok_matching : i1
    %ok37 = arith.andi %ok36, %ok_resize_u : i1
    %ok38 = arith.andi %ok37, %ok_concat : i1
    %ok39 = arith.andi %ok38, %ok_replicate : i1
    %ok40 = arith.andi %ok39, %ok_extract : i1
    %ok41 = arith.andi %ok40, %ok_insert : i1
    %ok42 = arith.andi %ok41, %ok_shifted_left : i1
    %ok43 = arith.andi %ok42, %ok_shifted : i1
    %ok44 = arith.andi %ok43, %ok_shifted_arith : i1
    return %ok44 : i1
  }

  func.func @main() -> i32 attributes {llvm.emit_c_interface} {
    %false = arith.constant false
    %true = arith.constant true

    // Scalar 0/1/X/Z control truth.
    %zero1 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
    %one1 = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
    %x1 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
    %z1 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
    %truth0 = simulation.logic.is_true %zero1 : !simulation.logic<1>
    %truth1 = simulation.logic.is_true %one1 : !simulation.logic<1>
    %truthx = simulation.logic.is_true %x1 : !simulation.logic<1>
    %truthz = simulation.logic.is_true %z1 : !simulation.logic<1>
    %ok_t0 = arith.cmpi eq, %truth0, %false : i1
    %ok_t1 = arith.cmpi eq, %truth1, %true : i1
    %ok_tx = arith.cmpi eq, %truthx, %false : i1
    %ok_tz = arith.cmpi eq, %truthz, %false : i1
    %ok_t01 = arith.andi %ok_t0, %ok_t1 : i1
    %ok_txz = arith.andi %ok_tx, %ok_tz : i1
    %ok_truth = arith.andi %ok_t01, %ok_txz : i1

    // Dominating known bits and newly generated X values.
    %x_and_zero = simulation.logic.binary and %x1, %zero1 : !simulation.logic<1>
    %x_or_one = simulation.logic.binary or %x1, %one1 : !simulation.logic<1>
    %x_xor_one = simulation.logic.binary xor %x1, %one1 : !simulation.logic<1>
    %ok_and = simulation.logic.compare case_eq %x_and_zero, %zero1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_or = simulation.logic.compare case_eq %x_or_one, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_xor = simulation.logic.compare case_eq %x_xor_one, %x1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1
    %ok_bit_0 = arith.andi %ok_and, %ok_or : i1
    %ok_bit = arith.andi %ok_bit_0, %ok_xor : i1

    // Width 5 signed extension copies the exact sign state, including Z.
    %sign_z5 = simulation.logic.constant 16 : i5, 16 : i5 : !simulation.logic<5>
    %sign_z37 = simulation.logic.resize %sign_z5 signed = true : !simulation.logic<5> -> !simulation.logic<37>
    %expected_z37 = simulation.logic.constant 137438953456 : i37, 137438953456 : i37 : !simulation.logic<37>
    %ok_resize = simulation.logic.compare case_eq %sign_z37, %expected_z37 : (!simulation.logic<37>, !simulation.logic<37>) -> i1

    // Width 37 known arithmetic.
    %a37 = simulation.logic.constant 68719476731 : i37, 0 : i37 : !simulation.logic<37>
    %b37 = simulation.logic.constant 7 : i37, 0 : i37 : !simulation.logic<37>
    %sum37 = simulation.logic.binary add %a37, %b37 : !simulation.logic<37>
    %expected_sum37 = simulation.logic.constant 68719476738 : i37, 0 : i37 : !simulation.logic<37>
    %ok_sum37 = simulation.logic.compare case_eq %sum37, %expected_sum37 : (!simulation.logic<37>, !simulation.logic<37>) -> i1

    // Width 65 known arithmetic and poison-free signed overflow.
    %two65 = simulation.logic.constant 2 : i65, 0 : i65 : !simulation.logic<65>
    %three65 = simulation.logic.constant 3 : i65, 0 : i65 : !simulation.logic<65>
    %five65 = simulation.logic.constant 5 : i65, 0 : i65 : !simulation.logic<65>
    %sum65 = simulation.logic.binary add %two65, %three65 : !simulation.logic<65>
    %ok_sum65 = simulation.logic.compare case_eq %sum65, %five65 : (!simulation.logic<65>, !simulation.logic<65>) -> i1
    %min65 = simulation.logic.constant 18446744073709551616 : i65, 0 : i65 : !simulation.logic<65>
    %minus_one65 = simulation.logic.constant -1 : i65, 0 : i65 : !simulation.logic<65>
    %overflow_div = simulation.logic.binary sdiv %min65, %minus_one65 : !simulation.logic<65>
    %overflow_mod = simulation.logic.binary smod %min65, %minus_one65 : !simulation.logic<65>
    %zero65 = simulation.logic.constant 0 : i65, 0 : i65 : !simulation.logic<65>
    %ok_overflow_div = simulation.logic.compare case_eq %overflow_div, %min65 : (!simulation.logic<65>, !simulation.logic<65>) -> i1
    %ok_overflow_mod = simulation.logic.compare case_eq %overflow_mod, %zero65 : (!simulation.logic<65>, !simulation.logic<65>) -> i1

    // Division and modulo by zero produce canonical all-X results.
    %ten5 = simulation.logic.constant 10 : i5, 0 : i5 : !simulation.logic<5>
    %zero5 = simulation.logic.constant 0 : i5, 0 : i5 : !simulation.logic<5>
    %all_x5 = simulation.logic.constant 0 : i5, -1 : i5 : !simulation.logic<5>
    %div_zero = simulation.logic.binary udiv %ten5, %zero5 : !simulation.logic<5>
    %mod_zero = simulation.logic.binary umod %ten5, %zero5 : !simulation.logic<5>
    %ok_div_zero = simulation.logic.compare case_eq %div_zero, %all_x5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_mod_zero = simulation.logic.compare case_eq %mod_zero, %all_x5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1

    %minus_two5 = simulation.logic.constant -2 : i5, 0 : i5 : !simulation.logic<5>
    %signed_lt = simulation.logic.compare slt %minus_two5, %ten5 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ok_signed_cmp = simulation.logic.compare case_eq %signed_lt, %one1 : (!simulation.logic<1>, !simulation.logic<1>) -> i1

    // Unknown and oversized shifts never feed an invalid amount to arith.
    %value5 = simulation.logic.constant 17 : i5, 4 : i5 : !simulation.logic<5>
    %unknown_amount = simulation.logic.constant 1 : i5, 1 : i5 : !simulation.logic<5>
    %large_amount = simulation.logic.constant 7 : i5, 0 : i5 : !simulation.logic<5>
    %unknown_shift = simulation.logic.shift left %value5 by %unknown_amount : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %large_shift = simulation.logic.shift left %value5 by %large_amount : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %large_ashr = simulation.logic.shift right_arith %sign_z5 by %large_amount : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %all_z5 = simulation.logic.constant -1 : i5, -1 : i5 : !simulation.logic<5>
    %ok_unknown_shift = simulation.logic.compare case_eq %unknown_shift, %all_x5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_large_shift = simulation.logic.compare case_eq %large_shift, %zero5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_large_ashr = simulation.logic.compare case_eq %large_ashr, %all_z5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1

    // Dynamic selections: valid, negative overlap, high overlap, fully out of
    // range, and unknown. Four-state invalid bits are X; two-state bits are 0.
    %idx1 = simulation.logic.constant 1 : i5, 0 : i5 : !simulation.logic<5>
    %idx_neg1 = simulation.logic.constant -1 : i5, 0 : i5 : !simulation.logic<5>
    %idx4 = simulation.logic.constant 4 : i5, 0 : i5 : !simulation.logic<5>
    %idx6 = simulation.logic.constant 6 : i5, 0 : i5 : !simulation.logic<5>
    %idx_unknown = simulation.logic.constant 0 : i5, 1 : i5 : !simulation.logic<5>
    %sel1 = simulation.logic.dyn_extract %value5 from %idx1 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<3>
    %sel_neg1 = simulation.logic.dyn_extract %value5 from %idx_neg1 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<3>
    %sel4 = simulation.logic.dyn_extract %value5 from %idx4 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<3>
    %sel6 = simulation.logic.dyn_extract %value5 from %idx6 : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<3>
    %sel_unknown = simulation.logic.dyn_extract %value5 from %idx_unknown : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<3>
    %expected_sel1 = simulation.logic.constant 0 : i3, 2 : i3 : !simulation.logic<3>
    %expected_sel_neg1 = simulation.logic.constant 2 : i3, 1 : i3 : !simulation.logic<3>
    %expected_sel4 = simulation.logic.constant 1 : i3, 6 : i3 : !simulation.logic<3>
    %all_x3 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
    %ok_sel1 = simulation.logic.compare case_eq %sel1, %expected_sel1 : (!simulation.logic<3>, !simulation.logic<3>) -> i1
    %ok_sel_neg1 = simulation.logic.compare case_eq %sel_neg1, %expected_sel_neg1 : (!simulation.logic<3>, !simulation.logic<3>) -> i1
    %ok_sel4 = simulation.logic.compare case_eq %sel4, %expected_sel4 : (!simulation.logic<3>, !simulation.logic<3>) -> i1
    %ok_sel6 = simulation.logic.compare case_eq %sel6, %all_x3 : (!simulation.logic<3>, !simulation.logic<3>) -> i1
    %ok_sel_unknown = simulation.logic.compare case_eq %sel_unknown, %all_x3 : (!simulation.logic<3>, !simulation.logic<3>) -> i1

    %bits5 = arith.constant 21 : i5
    %bits_neg1 = simulation.bits.dyn_extract %bits5 from %idx_neg1 : (i5, !simulation.logic<5>) -> i3
    %bits4 = simulation.bits.dyn_extract %bits5 from %idx4 : (i5, !simulation.logic<5>) -> i3
    %bits6 = simulation.bits.dyn_extract %bits5 from %idx6 : (i5, !simulation.logic<5>) -> i3
    %bits_unknown = simulation.bits.dyn_extract %bits5 from %idx_unknown : (i5, !simulation.logic<5>) -> i3
    %c2_i3 = arith.constant 2 : i3
    %c1_i3 = arith.constant 1 : i3
    %c0_i3 = arith.constant 0 : i3
    %ok_bits_neg1 = arith.cmpi eq, %bits_neg1, %c2_i3 : i3
    %ok_bits4 = arith.cmpi eq, %bits4, %c1_i3 : i3
    %ok_bits6 = arith.cmpi eq, %bits6, %c0_i3 : i3
    %ok_bits_unknown = arith.cmpi eq, %bits_unknown, %c0_i3 : i3

    // Dynamic replacements use the write-side bounds rules: unknown and
    // wholly invalid indices are no-ops, while partial overlaps update only
    // the in-range destination bits.
    %replacement3 = simulation.logic.constant 6 : i3, 2 : i3 : !simulation.logic<3>
    %insert_neg1 = simulation.logic.dyn_insert %replacement3 into %value5 at %idx_neg1 : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
    %insert4 = simulation.logic.dyn_insert %replacement3 into %value5 at %idx4 : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
    %insert6 = simulation.logic.dyn_insert %replacement3 into %value5 at %idx6 : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
    %insert_unknown = simulation.logic.dyn_insert %replacement3 into %value5 at %idx_unknown : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
    %expected_insert_neg1 = simulation.logic.constant 19 : i5, 5 : i5 : !simulation.logic<5>
    %expected_insert4 = simulation.logic.constant 1 : i5, 4 : i5 : !simulation.logic<5>
    %ok_insert_neg1 = simulation.logic.compare case_eq %insert_neg1, %expected_insert_neg1 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_insert4 = simulation.logic.compare case_eq %insert4, %expected_insert4 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_insert6 = simulation.logic.compare case_eq %insert6, %value5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ok_insert_unknown = simulation.logic.compare case_eq %insert_unknown, %value5 : (!simulation.logic<5>, !simulation.logic<5>) -> i1

    %bits_replacement3 = arith.constant 6 : i3
    %bits_insert_neg1 = simulation.bits.dyn_insert %bits_replacement3 into %bits5 at %idx_neg1 : (i5, i3, !simulation.logic<5>) -> i5
    %bits_insert4 = simulation.bits.dyn_insert %bits_replacement3 into %bits5 at %idx4 : (i5, i3, !simulation.logic<5>) -> i5
    %bits_insert6 = simulation.bits.dyn_insert %bits_replacement3 into %bits5 at %idx6 : (i5, i3, !simulation.logic<5>) -> i5
    %bits_insert_unknown = simulation.bits.dyn_insert %bits_replacement3 into %bits5 at %idx_unknown : (i5, i3, !simulation.logic<5>) -> i5
    %c23_i5 = arith.constant 23 : i5
    %c5_i5 = arith.constant 5 : i5
    %ok_bits_insert_neg1 = arith.cmpi eq, %bits_insert_neg1, %c23_i5 : i5
    %ok_bits_insert4 = arith.cmpi eq, %bits_insert4, %c5_i5 : i5
    %ok_bits_insert6 = arith.cmpi eq, %bits_insert6, %bits5 : i5
    %ok_bits_insert_unknown = arith.cmpi eq, %bits_insert_unknown, %bits5 : i5

    // An unsigned source index is normalized by zero-extension before the
    // signed two's-complement dynamic-index contract is applied.
    %wide_bits37 = arith.constant 2147483648 : i37
    %idx31_i6 = arith.constant 31 : i6
    %bit31 = simulation.bits.dyn_extract %wide_bits37 from %idx31_i6 : (i37, i6) -> i1
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
    %ok_insert0 = arith.andi %ok_selection, %ok_insert_neg1 : i1
    %ok_insert1 = arith.andi %ok_insert0, %ok_insert4 : i1
    %ok_insert2 = arith.andi %ok_insert1, %ok_insert6 : i1
    %ok_insert3 = arith.andi %ok_insert2, %ok_insert_unknown : i1
    %ok_insert4_all = arith.andi %ok_insert3, %ok_bits_insert_neg1 : i1
    %ok_insert5 = arith.andi %ok_insert4_all, %ok_bits_insert4 : i1
    %ok_insert6_all = arith.andi %ok_insert5, %ok_bits_insert6 : i1
    %ok_insert = arith.andi %ok_insert6_all, %ok_bits_insert_unknown : i1
    %remaining = func.call @exercise_remaining_ops() : () -> i1
    %ok21 = arith.andi %ok_insert, %remaining : i1
    %result = arith.extui %ok21 : i1 to i32
    return %result : i32
  }
}
