!test_array = !obelisk_sim.unpacked_array<1 : 0 x i8>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @generated_arithmetic {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.generated_arithmetic.arithmetic_process.9000001"
    obelisk_sim.scope.decl 0

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
      %is_expected = arith.andi %arithmetic_ok, %indices_ok : i1
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
