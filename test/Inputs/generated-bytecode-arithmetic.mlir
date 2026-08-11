!test_array = !obelisk_sim.unpacked_array<1 : 0 x i8>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @generated_arithmetic {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.generated_arithmetic.arithmetic_process.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.generated_arithmetic.store_x.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<129> design

    obelisk_sim.func private @store_x(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %x = obelisk_sim.logic.constant 0 : i8, -1 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.ref.store %x to %ref : !obelisk_sim.logic<8>,
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    obelisk_sim.func @arithmetic_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %three = arith.constant 3 : i64
      %product = arith.muli %one, %three : i64
      %quotient = arith.divui %product, %three : i64
      %remainder = arith.remui %product, %three : i64
      %shifted = arith.shli %quotient, %one : i64
      %unshifted = arith.shrui %shifted, %one : i64
      %quotient_ok = arith.cmpi eq, %unshifted, %one : i64
      %remainder_ok = arith.cmpi eq, %remainder, %zero : i64
      %unsigned_ok = arith.andi %quotient_ok, %remainder_ok : i1

      %minus_seven = arith.constant -7 : i64
      %minus_two = arith.constant -2 : i64
      %minus_one = arith.constant -1 : i64
      %signed_quotient = arith.divsi %minus_seven, %three : i64
      %signed_remainder = arith.remsi %minus_seven, %three : i64
      %signed_quotient_ok = arith.cmpi eq, %signed_quotient, %minus_two : i64
      %signed_remainder_ok = arith.cmpi eq, %signed_remainder, %minus_one : i64
      %signed_ok = arith.andi %signed_quotient_ok, %signed_remainder_ok : i1

      %one_i1 = arith.constant 1 : i1
      %product_i1 = arith.muli %one_i1, %one_i1 : i1
      %ok_i1 = arith.cmpi eq, %product_i1, %one_i1 : i1
      %seven_i5 = arith.constant 7 : i5
      %five_i5 = arith.constant 5 : i5
      %three_i5 = arith.constant 3 : i5
      %product_i5 = arith.muli %seven_i5, %five_i5 : i5
      %ok_i5 = arith.cmpi eq, %product_i5, %three_i5 : i5
      %lhs_i65 = arith.constant 18446744073709551617 : i65
      %three_i65 = arith.constant 3 : i65
      %expected_i65 = arith.constant 18446744073709551619 : i65
      %product_i65 = arith.muli %lhs_i65, %three_i65 : i65
      %ok_i65 = arith.cmpi eq, %product_i65, %expected_i65 : i65
      %lhs_i129 = arith.constant 18446744073709551616 : i129
      %three_i129 = arith.constant 3 : i129
      %expected_i129 = arith.constant 55340232221128654848 : i129
      %product_i129 = arith.muli %lhs_i129, %three_i129 : i129
      %ok_i129 = arith.cmpi eq, %product_i129, %expected_i129 : i129
      %dense_i129 = arith.constant 18446744073709551615 : i129
      %dense_expected_i129 = arith.constant 340282366920938463426481119284349108225 : i129
      %dense_product_i129 = arith.muli %dense_i129, %dense_i129 : i129
      %dense_ok_i129 = arith.cmpi eq, %dense_product_i129, %dense_expected_i129 : i129
      %carry_i129 = arith.constant 680564733841876926926749214863536422911 : i129
      %carry_product_i129 = arith.muli %carry_i129, %carry_i129 : i129
      %carry_expected_i129 = arith.constant 1 : i129
      %carry_ok_i129 = arith.cmpi eq, %carry_product_i129, %carry_expected_i129 : i129

      %array_left = arith.constant 42 : i8
      %array_right = arith.constant 43 : i8
      %array = obelisk_sim.aggregate.construct %array_left, %array_right :
          (i8, i8) -> !test_array
      %overflow_index = arith.constant 18446744073709551616 : i129
      %overflow_element = obelisk_sim.array.extract_dynamic
          %array[%overflow_index] : (!test_array, i129) -> i8
      %zero_i8 = arith.constant 0 : i8
      %overflow_ok = arith.cmpi eq, %overflow_element, %zero_i8 : i8
      %unknown_index = obelisk_sim.logic.constant 0 : i129,
          18446744073709551616 : i129 : !obelisk_sim.logic<129>
      %unknown_element = obelisk_sim.array.extract_dynamic
          %array[%unknown_index] :
          (!test_array, !obelisk_sim.logic<129>) -> i8
      %unknown_ok = arith.cmpi eq, %unknown_element, %zero_i8 : i8

      %small_ok = arith.andi %ok_i1, %ok_i5 : i1
      %wide_ok = arith.andi %ok_i65, %ok_i129 : i1
      %dense_ok = arith.andi %dense_ok_i129, %carry_ok_i129 : i1
      %wide_products_ok = arith.andi %wide_ok, %dense_ok : i1
      %integer_widths_ok = arith.andi %small_ok, %wide_products_ok : i1
      %indices_ok = arith.andi %overflow_ok, %unknown_ok : i1
      %division_ok = arith.andi %unsigned_ok, %signed_ok : i1
      %arithmetic_ok = arith.andi %integer_widths_ok, %division_ok : i1
      // Keep a proven two-state logic island live through bytecode generation.
      // The generated runtime test executes this process through both native
      // and bytecode tiers, so one-plane register semantics are compared
      // directly instead of only being checked in serialized IR.
      %logic_zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %logic_one = obelisk_sim.logic.unary bit_not %logic_zero :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      %logic_ok = obelisk_sim.logic.is_true %logic_one : !obelisk_sim.logic<1>
      %logic65_lhs = obelisk_sim.logic.from_bits %lhs_i65 :
          i65 -> !obelisk_sim.logic<65>
      %logic65_zero = obelisk_sim.logic.constant 0 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      %logic65_sum = obelisk_sim.logic.binary add %logic65_lhs,
          %logic65_zero : !obelisk_sim.logic<65>
      %logic65_bits = obelisk_sim.logic.to_bits %logic65_sum :
          !obelisk_sim.logic<65> -> i65
      %logic65_ok = arith.cmpi eq, %logic65_bits, %lhs_i65 : i65
      %logic129_lhs = obelisk_sim.logic.from_bits %lhs_i129 :
          i129 -> !obelisk_sim.logic<129>
      %logic129_not = obelisk_sim.logic.unary bit_not %logic129_lhs :
          (!obelisk_sim.logic<129>) -> !obelisk_sim.logic<129>
      %logic129_roundtrip = obelisk_sim.logic.unary bit_not %logic129_not :
          (!obelisk_sim.logic<129>) -> !obelisk_sim.logic<129>
      %logic129_bits = obelisk_sim.logic.to_bits %logic129_roundtrip :
          !obelisk_sim.logic<129> -> i129
      %logic129_ok = arith.cmpi eq, %logic129_bits, %lhs_i129 : i129
      %wide_logic_ok = arith.andi %logic65_ok, %logic129_ok : i1

      // Exercise the bytecode-only four-state arithmetic paths that cannot be
      // covered by the builtin-integer operations above.
      %logic_x1 = obelisk_sim.logic.constant 0 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      %reduction_input = obelisk_sim.logic.constant 21 : i5, 2 : i5 :
          !obelisk_sim.logic<5>
      %reduce_and = obelisk_sim.logic.reduction and %reduction_input :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
      %reduce_nand = obelisk_sim.logic.reduction nand %reduction_input :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
      %reduce_or = obelisk_sim.logic.reduction or %reduction_input :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
      %reduce_nor = obelisk_sim.logic.reduction nor %reduction_input :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
      %reduce_xor = obelisk_sim.logic.reduction xor %reduction_input :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
      %reduce_xnor = obelisk_sim.logic.reduction xnor %reduction_input :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
      %reduce_and_ok = obelisk_sim.logic.compare case_eq %reduce_and,
          %logic_zero : (!obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>) -> i1
      %reduce_nand_ok = obelisk_sim.logic.compare case_eq %reduce_nand,
          %logic_one : (!obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>) -> i1
      %reduce_or_ok = obelisk_sim.logic.compare case_eq %reduce_or,
          %logic_one : (!obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>) -> i1
      %reduce_nor_ok = obelisk_sim.logic.compare case_eq %reduce_nor,
          %logic_zero : (!obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>) -> i1
      %reduce_xor_ok = obelisk_sim.logic.compare case_eq %reduce_xor,
          %logic_x1 : (!obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>) -> i1
      %reduce_xnor_ok = obelisk_sim.logic.compare case_eq %reduce_xnor,
          %logic_x1 : (!obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>) -> i1
      %reductions0_ok = arith.andi %reduce_and_ok, %reduce_nand_ok : i1
      %reductions1_ok = arith.andi %reduce_or_ok, %reduce_nor_ok : i1
      %reductions2_ok = arith.andi %reduce_xor_ok, %reduce_xnor_ok : i1
      %reductions3_ok = arith.andi %reductions0_ok, %reductions1_ok : i1
      %reductions_ok = arith.andi %reductions3_ok, %reductions2_ok : i1

      %logic_minus_ten = obelisk_sim.logic.constant -10 : i5, 0 : i5 :
          !obelisk_sim.logic<5>
      %logic_shift_one = obelisk_sim.logic.constant 1 : i5, 0 : i5 :
          !obelisk_sim.logic<5>
      %logic_minus_five = obelisk_sim.logic.constant -5 : i5, 0 : i5 :
          !obelisk_sim.logic<5>
      %arithmetic_shift = obelisk_sim.logic.shift right_arith
          %logic_minus_ten by %logic_shift_one :
          (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) ->
          !obelisk_sim.logic<5>
      %arithmetic_shift_ok = obelisk_sim.logic.compare case_eq
          %arithmetic_shift, %logic_minus_five :
          (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1

      %logic_ten = obelisk_sim.logic.constant 10 : i5, 0 : i5 :
          !obelisk_sim.logic<5>
      %logic_zero5 = obelisk_sim.logic.constant 0 : i5, 0 : i5 :
          !obelisk_sim.logic<5>
      %logic_all_x5 = obelisk_sim.logic.constant 0 : i5, -1 : i5 :
          !obelisk_sim.logic<5>
      %division_by_zero = obelisk_sim.logic.binary udiv %logic_ten,
          %logic_zero5 : !obelisk_sim.logic<5>
      %modulo_by_zero = obelisk_sim.logic.binary umod %logic_ten,
          %logic_zero5 : !obelisk_sim.logic<5>
      %division_by_zero_ok = obelisk_sim.logic.compare case_eq
          %division_by_zero, %logic_all_x5 :
          (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
      %modulo_by_zero_ok = obelisk_sim.logic.compare case_eq
          %modulo_by_zero, %logic_all_x5 :
          (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
      %zero_division_ok = arith.andi %division_by_zero_ok,
          %modulo_by_zero_ok : i1

      %logic_mixed5 = obelisk_sim.logic.constant 1 : i5, 2 : i5 :
          !obelisk_sim.logic<5>
      %unknown_sum = obelisk_sim.logic.binary add %logic_ten,
          %logic_mixed5 : !obelisk_sim.logic<5>
      %unknown_product = obelisk_sim.logic.binary mul %logic_ten,
          %logic_mixed5 : !obelisk_sim.logic<5>
      %unknown_quotient = obelisk_sim.logic.binary udiv %logic_ten,
          %logic_mixed5 : !obelisk_sim.logic<5>
      %unknown_sum_ok = obelisk_sim.logic.compare case_eq %unknown_sum,
          %logic_all_x5 : (!obelisk_sim.logic<5>,
          !obelisk_sim.logic<5>) -> i1
      %unknown_product_ok = obelisk_sim.logic.compare case_eq %unknown_product,
          %logic_all_x5 : (!obelisk_sim.logic<5>,
          !obelisk_sim.logic<5>) -> i1
      %unknown_quotient_ok = obelisk_sim.logic.compare case_eq
          %unknown_quotient, %logic_all_x5 :
          (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
      %unknown_arithmetic0_ok = arith.andi %unknown_sum_ok,
          %unknown_product_ok : i1
      %unknown_arithmetic_ok = arith.andi %unknown_arithmetic0_ok,
          %unknown_quotient_ok : i1

      %logic_min65 = obelisk_sim.logic.constant 18446744073709551616 : i65,
          0 : i65 : !obelisk_sim.logic<65>
      %logic_minus_one65 = obelisk_sim.logic.constant -1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      %logic_overflow_div = obelisk_sim.logic.binary sdiv %logic_min65,
          %logic_minus_one65 : !obelisk_sim.logic<65>
      %logic_overflow_mod = obelisk_sim.logic.binary smod %logic_min65,
          %logic_minus_one65 : !obelisk_sim.logic<65>
      %logic_overflow_div_ok = obelisk_sim.logic.compare case_eq
          %logic_overflow_div, %logic_min65 :
          (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> i1
      %logic_overflow_mod_ok = obelisk_sim.logic.compare case_eq
          %logic_overflow_mod, %logic65_zero :
          (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> i1
      %logic_overflow_ok = arith.andi %logic_overflow_div_ok,
          %logic_overflow_mod_ok : i1

      // A mux whose condition and arms are all proven two-state gets one-plane
      // registers, which cannot hold the unknown-condition merge. Encoding it
      // as a four-state select produces an image the runtime rejects, so the
      // select must follow the registers it was given.
      %mux_taken = obelisk_sim.logic.constant 21 : i6, 0 : i6 :
          !obelisk_sim.logic<6>
      %mux_skipped = obelisk_sim.logic.constant 42 : i6, 0 : i6 :
          !obelisk_sim.logic<6>
      %mux_true_cond = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %two_state_mux = obelisk_sim.logic.mux %mux_true_cond ? %mux_taken :
          %mux_skipped : (!obelisk_sim.logic<1>, !obelisk_sim.logic<6>,
          !obelisk_sim.logic<6>) -> !obelisk_sim.logic<6>
      %two_state_mux_ok = obelisk_sim.logic.compare case_eq %two_state_mux,
          %mux_taken : (!obelisk_sim.logic<6>,
          !obelisk_sim.logic<6>) -> i1
      // An unknown condition still merges the arms bitwise: every bit the arms
      // disagree on, or that either arm leaves unknown, becomes X.
      %mux_unknown_cond = obelisk_sim.logic.constant 0 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      %mux_left = obelisk_sim.logic.constant 10 : i6, 0 : i6 :
          !obelisk_sim.logic<6>
      %mux_right = obelisk_sim.logic.constant 1 : i6, 2 : i6 :
          !obelisk_sim.logic<6>
      %unknown_mux = obelisk_sim.logic.mux %mux_unknown_cond ? %mux_left :
          %mux_right : (!obelisk_sim.logic<1>, !obelisk_sim.logic<6>,
          !obelisk_sim.logic<6>) -> !obelisk_sim.logic<6>
      %unknown_mux_expected = obelisk_sim.logic.constant 0 : i6, 11 : i6 :
          !obelisk_sim.logic<6>
      %unknown_mux_ok = obelisk_sim.logic.compare case_eq %unknown_mux,
          %unknown_mux_expected : (!obelisk_sim.logic<6>,
          !obelisk_sim.logic<6>) -> i1
      %mux_ok = arith.andi %two_state_mux_ok, %unknown_mux_ok : i1

      %four_state0_ok = arith.andi %reductions_ok, %arithmetic_shift_ok : i1
      %four_state1_ok = arith.andi %zero_division_ok,
          %unknown_arithmetic_ok : i1
      %four_state2_ok = arith.andi %four_state0_ok, %four_state1_ok : i1
      %four_state3_ok = arith.andi %four_state2_ok, %logic_overflow_ok : i1
      %four_state_ok = arith.andi %four_state3_ok, %mux_ok : i1
      %base_logic_ok = arith.andi %logic_ok, %wide_logic_ok : i1
      %all_logic_ok = arith.andi %base_logic_ok, %four_state_ok : i1
      // A known initializer must not turn automatic logic storage into a
      // permanently two-state allocation.  Escape the reference through a
      // call, write X there, and make the process lifecycle depend on seeing
      // that X after the call.
      %automatic_initial = obelisk_sim.logic.constant 5 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %automatic = obelisk_sim.ref.alloc %automatic_initial :
          !obelisk_sim.logic<8> ->
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.call @store_x(%ctx, %automatic) :
          (!obelisk_sim.context,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %automatic_loaded = obelisk_sim.ref.load %automatic :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> ->
          !obelisk_sim.logic<8>
      %automatic_x = obelisk_sim.logic.constant 0 : i8, -1 : i8 :
          !obelisk_sim.logic<8>
      %automatic_ok = obelisk_sim.logic.compare case_eq %automatic_loaded,
          %automatic_x : (!obelisk_sim.logic<8>,
          !obelisk_sim.logic<8>) -> i1
      %wide_storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<129>>
      obelisk_sim.ref.store %logic129_roundtrip to %wide_storage :
          !obelisk_sim.logic<129>, !obelisk_sim.ref<!obelisk_sim.logic<129>>
      %arithmetic_and_indices = arith.andi %arithmetic_ok, %indices_ok : i1
      %logic_and_automatic_ok = arith.andi %all_logic_ok, %automatic_ok : i1
      %is_expected = arith.andi %arithmetic_and_indices,
          %logic_and_automatic_ok : i1
      cf.cond_br %is_expected, ^expected, ^unexpected
    ^expected:
      %delay = obelisk_sim.time.constant 7
      obelisk_sim.suspend.delay %delay to ^done
    ^unexpected:
      obelisk_sim.return
    ^done:
      obelisk_sim.return
    }
  }
}
