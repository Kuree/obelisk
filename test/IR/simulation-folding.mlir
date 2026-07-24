// RUN: obelisk-opt %s -canonicalize | FileCheck %s

module {
  obelisk_sim.design @folding {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.folding.constants.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.folding.controlling_bitwise.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.folding.resize_chains.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 function hierarchy "test.folding.operand_order.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 function hierarchy "test.folding.structural.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 function hierarchy "test.folding.matching.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 function hierarchy "test.folding.pure_inquiries.9000007"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @constants(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> (!obelisk_sim.logic<4>, i4, i1) attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %x = obelisk_sim.logic.constant 10 : i4, 4 : i4 : !obelisk_sim.logic<4>
      %y = obelisk_sim.logic.constant 3 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %add = obelisk_sim.logic.binary add %x, %y : !obelisk_sim.logic<4>
      %bits = obelisk_sim.logic.to_bits %add : !obelisk_sim.logic<4> -> i4
      %truth = obelisk_sim.logic.is_true %add : !obelisk_sim.logic<4>
      obelisk_sim.return %add, %bits, %truth : !obelisk_sim.logic<4>, i4, i1
    }

    obelisk_sim.func @controlling_bitwise(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}) -> (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>) attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %ones = obelisk_sim.logic.constant -1 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %and_zero = obelisk_sim.logic.binary and %value, %zero : !obelisk_sim.logic<4>
      %or_ones = obelisk_sim.logic.binary or %value, %ones : !obelisk_sim.logic<4>
      %and_ones = obelisk_sim.logic.binary and %value, %ones : !obelisk_sim.logic<4>
      %or_zero = obelisk_sim.logic.binary or %value, %zero : !obelisk_sim.logic<4>
      obelisk_sim.return %and_zero, %or_ones, %and_ones, %or_zero : !obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>
    }

    obelisk_sim.func @resize_chains(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) -> (!obelisk_sim.logic<12>, !obelisk_sim.logic<32>) attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %wide = obelisk_sim.logic.resize %value signed = true : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %collapsed = obelisk_sim.logic.resize %wide signed = false : !obelisk_sim.logic<16> -> !obelisk_sim.logic<12>
      %preserved = obelisk_sim.logic.resize %wide signed = false : !obelisk_sim.logic<16> -> !obelisk_sim.logic<32>
      obelisk_sim.return %collapsed, %preserved : !obelisk_sim.logic<12>, !obelisk_sim.logic<32>
    }

    obelisk_sim.func @operand_order(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}) -> (!obelisk_sim.logic<4>, !obelisk_sim.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %one = obelisk_sim.logic.constant 1 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %sum = obelisk_sim.logic.binary add %one, %value : !obelisk_sim.logic<4>
      %less = obelisk_sim.logic.compare ult %one, %value : (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<1>
      obelisk_sim.return %sum, %less : !obelisk_sim.logic<4>, !obelisk_sim.logic<1>
    }

    obelisk_sim.func @structural(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %a: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}, %b: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}, %base: !obelisk_sim.logic<16> {obelisk_sim.capture_kind = 2 : i32}, %r4: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}, %r8: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) -> (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>, !obelisk_sim.logic<8>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<2>, !obelisk_sim.logic<16>) attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %repeated = obelisk_sim.logic.concat %a, %a : (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      %repeat_slice = obelisk_sim.logic.extract %repeated from 4 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      %concat = obelisk_sim.logic.concat %a, %b : (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      %concat_low = obelisk_sim.logic.extract %concat from 0 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      %low = obelisk_sim.logic.extract %base from 0 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<4>
      %high = obelisk_sim.logic.extract %base from 4 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<4>
      %adjacent = obelisk_sim.logic.concat %high, %low : (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      %inserted = obelisk_sim.logic.insert %r8 into %base at 4 : (!obelisk_sim.logic<16>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<16>
      %disjoint = obelisk_sim.logic.extract %inserted from 0 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<4>
      %inside = obelisk_sim.logic.extract %inserted from 6 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<2>
      %inner = obelisk_sim.logic.insert %r4 into %base at 6 : (!obelisk_sim.logic<16>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<16>
      %shadowed = obelisk_sim.logic.insert %r8 into %inner at 4 : (!obelisk_sim.logic<16>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<16>
      obelisk_sim.return %repeated, %repeat_slice, %adjacent, %concat_low, %disjoint, %inside, %shadowed : !obelisk_sim.logic<8>, !obelisk_sim.logic<4>, !obelisk_sim.logic<8>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<2>, !obelisk_sim.logic<16>
    }

    obelisk_sim.func @matching_fold(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1, i1, i1, i1) attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 : !obelisk_sim.logic<1>
      %wild_mask = obelisk_sim.logic.compare wild_eq %x, %x : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      %wild_unknown = obelisk_sim.logic.compare wild_eq %x, %zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      %wild_ne = obelisk_sim.logic.compare wild_ne %one, %zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      %casez_z = obelisk_sim.logic.compare casez_eq %z, %zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      %casez_x = obelisk_sim.logic.compare casez_eq %x, %zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      %casez_exact_x = obelisk_sim.logic.compare casez_eq %x, %x : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      %casex = obelisk_sim.logic.compare casexz_eq %x, %zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.return %wild_mask, %wild_unknown, %wild_ne, %casez_z, %casez_x, %casez_exact_x, %casex : !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1, i1, i1, i1
    }

    obelisk_sim.func @pure_inquiries(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> (i32, i32, i32, i32) attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %mixed = obelisk_sim.logic.constant 1477 : i12, 780 : i12 : !obelisk_sim.logic<12>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 : !obelisk_sim.logic<1>
      %count_zero = obelisk_sim.logic.count_bits %mixed matching %zero : (!obelisk_sim.logic<12>, !obelisk_sim.logic<1>) -> i32
      %count_all = obelisk_sim.logic.count_bits %mixed matching %zero, %one, %x, %z, %one : (!obelisk_sim.logic<12>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i32
      %clog2 = obelisk_sim.logic.clog2 %mixed : !obelisk_sim.logic<12>
      %clog2_zero = obelisk_sim.logic.clog2 %zero : !obelisk_sim.logic<1>
      obelisk_sim.return %count_zero, %count_all, %clog2, %clog2_zero : i32, i32, i32, i32
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @constants
// CHECK: %[[LOGIC:.*]] = obelisk_sim.logic.constant 0 : i4, -1 : i4
// CHECK: %[[BITS:.*]] = arith.constant 0 : i4
// CHECK: %[[TRUE:.*]] = arith.constant false
// CHECK: obelisk_sim.return %[[LOGIC]], %[[BITS]], %[[TRUE]]

// CHECK-LABEL: obelisk_sim.func @controlling_bitwise
// CHECK: %[[ZERO:.*]] = obelisk_sim.logic.constant 0 : i4, 0 : i4
// CHECK: %[[ONES:.*]] = obelisk_sim.logic.constant -1 : i4, 0 : i4
// CHECK: %[[AND:.*]] = obelisk_sim.logic.binary and %arg1, %[[ONES]]
// CHECK: %[[OR:.*]] = obelisk_sim.logic.binary or %arg1, %[[ZERO]]
// CHECK: obelisk_sim.return %[[ZERO]], %[[ONES]], %[[AND]], %[[OR]]

// CHECK-LABEL: obelisk_sim.func @resize_chains
// CHECK-DAG: %[[COLLAPSED:.*]] = obelisk_sim.logic.resize %arg1 signed = true : !obelisk_sim.logic<8> -> !obelisk_sim.logic<12>
// CHECK-DAG: %[[WIDE:.*]] = obelisk_sim.logic.resize %arg1 signed = true : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
// CHECK: %[[PRESERVED:.*]] = obelisk_sim.logic.resize %[[WIDE]] signed = false : !obelisk_sim.logic<16> -> !obelisk_sim.logic<32>
// CHECK: obelisk_sim.return %[[COLLAPSED]], %[[PRESERVED]]

// CHECK-LABEL: obelisk_sim.func @operand_order
// CHECK: %[[ONE:.*]] = obelisk_sim.logic.constant 1 : i4, 0 : i4
// CHECK: %[[SUM:.*]] = obelisk_sim.logic.binary add %arg1, %[[ONE]]
// CHECK: %[[CMP:.*]] = obelisk_sim.logic.compare ugt %arg1, %[[ONE]]
// CHECK: obelisk_sim.return %[[SUM]], %[[CMP]]

// CHECK-LABEL: obelisk_sim.func @structural
// CHECK: %[[REPEATED:.*]] = obelisk_sim.logic.replicate %arg1 times 2
// CHECK: %[[ADJACENT:.*]] = obelisk_sim.logic.extract %arg3 from 0 {{.*}}!obelisk_sim.logic<8>
// CHECK: %[[DISJOINT:.*]] = obelisk_sim.logic.extract %arg3 from 0
// CHECK: %[[INSIDE:.*]] = obelisk_sim.logic.extract %arg5 from 2
// CHECK: %[[SHADOWED:.*]] = obelisk_sim.logic.insert %arg5 into %arg3 at 4
// CHECK: obelisk_sim.return %[[REPEATED]], %arg1, %[[ADJACENT]], %arg2, %[[DISJOINT]], %[[INSIDE]], %[[SHADOWED]]

// CHECK-LABEL: obelisk_sim.func @matching_fold
// CHECK: %[[MATCH_ONE:.*]] = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
// CHECK: %[[MATCH_X:.*]] = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
// CHECK: %[[MATCH_TRUE:.*]] = arith.constant true
// CHECK: %[[MATCH_FALSE:.*]] = arith.constant false
// CHECK-NOT: obelisk_sim.logic.compare
// CHECK: obelisk_sim.return %[[MATCH_ONE]], %[[MATCH_X]], %[[MATCH_ONE]], %[[MATCH_TRUE]], %[[MATCH_FALSE]], %[[MATCH_TRUE]], %[[MATCH_TRUE]]

// CHECK-LABEL: obelisk_sim.func @pure_inquiries
// CHECK-DAG: %[[FOUR:.*]] = arith.constant 4 : i32
// CHECK-DAG: %[[TWELVE:.*]] = arith.constant 12 : i32
// CHECK-DAG: %[[ELEVEN:.*]] = arith.constant 11 : i32
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-NOT: obelisk_sim.logic.count_bits
// CHECK-NOT: obelisk_sim.logic.clog2
// CHECK: obelisk_sim.return %[[FOUR]], %[[TWELVE]], %[[ELEVEN]], %[[ZERO]]
