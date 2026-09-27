// RUN: obelisk-opt %s -canonicalize | FileCheck %s

module {
  simulation.design @folding {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.folding.constants.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.folding.controlling_bitwise.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.folding.resize_chains.9000003"
    simulation.code_unit.decl 9000004 in 0 function hierarchy "test.folding.operand_order.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.folding.structural.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.folding.matching.9000006"
    simulation.code_unit.decl 9000007 in 0 function hierarchy "test.folding.pure_inquiries.9000007"
    simulation.code_unit.decl 9000008 in 0 function hierarchy "test.folding.conditional_merge.9000008"
    simulation.code_unit.decl 9000009 in 0 function hierarchy "test.folding.dynamic_insert_constants.9000009"
    simulation.scope.decl 0
    simulation.func @constants(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (!simulation.logic<4>, i4, i1) attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %x = simulation.logic.constant 10 : i4, 4 : i4 : !simulation.logic<4>
      %y = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
      %add = simulation.logic.binary add %x, %y : !simulation.logic<4>
      %bits = simulation.logic.to_bits %add : !simulation.logic<4> -> i4
      %truth = simulation.logic.is_true %add : !simulation.logic<4>
      simulation.return %add, %bits, %truth : !simulation.logic<4>, i4, i1
    }

    simulation.func @dynamic_insert_constants(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>, i8, i8) attributes {entry_kind = 8 : i32, code_unit_id = 9000009 : i64} {
      %base = simulation.logic.constant 165 : i8, 16 : i8 : !simulation.logic<8>
      %replacement = simulation.logic.constant 6 : i3, 2 : i3 : !simulation.logic<3>
      %negative = arith.constant -1 : i4
      %high = arith.constant 7 : i4
      %outside = arith.constant -3 : i4
      %unknown = simulation.logic.constant 0 : i4, 1 : i4 : !simulation.logic<4>
      %partial_low = simulation.logic.dyn_insert %replacement into %base at %negative : (!simulation.logic<8>, !simulation.logic<3>, i4) -> !simulation.logic<8>
      %partial_high = simulation.logic.dyn_insert %replacement into %base at %high : (!simulation.logic<8>, !simulation.logic<3>, i4) -> !simulation.logic<8>
      %unchanged_unknown = simulation.logic.dyn_insert %replacement into %base at %unknown : (!simulation.logic<8>, !simulation.logic<3>, !simulation.logic<4>) -> !simulation.logic<8>
      %bits_base = arith.constant 165 : i8
      %bits_replacement = arith.constant 6 : i3
      %bits_partial = simulation.bits.dyn_insert %bits_replacement into %bits_base at %negative : (i8, i3, i4) -> i8
      %bits_unchanged = simulation.bits.dyn_insert %bits_replacement into %bits_base at %outside : (i8, i3, i4) -> i8
      simulation.return %partial_low, %partial_high, %unchanged_unknown, %bits_partial, %bits_unchanged : !simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>, i8, i8
    }

    simulation.func @controlling_bitwise(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<4> {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>) attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %zero = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      %ones = simulation.logic.constant -1 : i4, 0 : i4 : !simulation.logic<4>
      %and_zero = simulation.logic.binary and %value, %zero : !simulation.logic<4>
      %or_ones = simulation.logic.binary or %value, %ones : !simulation.logic<4>
      %and_ones = simulation.logic.binary and %value, %ones : !simulation.logic<4>
      %or_zero = simulation.logic.binary or %value, %zero : !simulation.logic<4>
      simulation.return %and_zero, %or_ones, %and_ones, %or_zero : !simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>
    }

    simulation.func @resize_chains(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<12>, !simulation.logic<32>) attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %wide = simulation.logic.resize %value signed = true : !simulation.logic<8> -> !simulation.logic<16>
      %collapsed = simulation.logic.resize %wide signed = false : !simulation.logic<16> -> !simulation.logic<12>
      %preserved = simulation.logic.resize %wide signed = false : !simulation.logic<16> -> !simulation.logic<32>
      simulation.return %collapsed, %preserved : !simulation.logic<12>, !simulation.logic<32>
    }

    simulation.func @operand_order(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<4> {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<4>, !simulation.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %one = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
      %sum = simulation.logic.binary add %one, %value : !simulation.logic<4>
      %less = simulation.logic.compare ult %one, %value : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
      simulation.return %sum, %less : !simulation.logic<4>, !simulation.logic<1>
    }

    simulation.func @structural(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.logic<4> {simulation.capture_kind = 2 : i32}, %b: !simulation.logic<4> {simulation.capture_kind = 2 : i32}, %base: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %r4: !simulation.logic<4> {simulation.capture_kind = 2 : i32}, %r8: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<8>, !simulation.logic<4>, !simulation.logic<8>, !simulation.logic<4>, !simulation.logic<4>, !simulation.logic<2>, !simulation.logic<16>, !simulation.logic<16>) attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %repeated = simulation.logic.concat %a, %a : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<8>
      %repeat_slice = simulation.logic.extract %repeated from 4 : !simulation.logic<8> -> !simulation.logic<4>
      %concat = simulation.logic.concat %a, %b : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<8>
      %concat_low = simulation.logic.extract %concat from 0 : !simulation.logic<8> -> !simulation.logic<4>
      %low = simulation.logic.extract %base from 0 : !simulation.logic<16> -> !simulation.logic<4>
      %high = simulation.logic.extract %base from 4 : !simulation.logic<16> -> !simulation.logic<4>
      %adjacent = simulation.logic.concat %high, %low : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<8>
      %inserted = simulation.logic.insert %r8 into %base at 4 : (!simulation.logic<16>, !simulation.logic<8>) -> !simulation.logic<16>
      %disjoint = simulation.logic.extract %inserted from 0 : !simulation.logic<16> -> !simulation.logic<4>
      %inside = simulation.logic.extract %inserted from 6 : !simulation.logic<16> -> !simulation.logic<2>
      %inner = simulation.logic.insert %r4 into %base at 6 : (!simulation.logic<16>, !simulation.logic<4>) -> !simulation.logic<16>
      %shadowed = simulation.logic.insert %r8 into %inner at 4 : (!simulation.logic<16>, !simulation.logic<8>) -> !simulation.logic<16>
      %known_index = arith.constant 4 : i32
      %dynamic_known = simulation.logic.dyn_insert %r4 into %base at %known_index : (!simulation.logic<16>, !simulation.logic<4>, i32) -> !simulation.logic<16>
      simulation.return %repeated, %repeat_slice, %adjacent, %concat_low, %disjoint, %inside, %shadowed, %dynamic_known : !simulation.logic<8>, !simulation.logic<4>, !simulation.logic<8>, !simulation.logic<4>, !simulation.logic<4>, !simulation.logic<2>, !simulation.logic<16>, !simulation.logic<16>
    }

    simulation.func @matching_fold(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (!simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, i1, i1, i1, i1) attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      %one = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
      %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      %wild_mask = simulation.logic.compare wild_eq %x, %x : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
      %wild_unknown = simulation.logic.compare wild_eq %x, %zero : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
      %wild_ne = simulation.logic.compare wild_ne %one, %zero : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
      %casez_z = simulation.logic.compare casez_eq %z, %zero : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %casez_x = simulation.logic.compare casez_eq %x, %zero : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %casez_exact_x = simulation.logic.compare casez_eq %x, %x : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %casex = simulation.logic.compare casexz_eq %x, %zero : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.return %wild_mask, %wild_unknown, %wild_ne, %casez_z, %casez_x, %casez_exact_x, %casex : !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, i1, i1, i1, i1
    }

    simulation.func @pure_inquiries(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (i32, i32, i32, i32) attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %mixed = simulation.logic.constant 1477 : i12, 780 : i12 : !simulation.logic<12>
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      %one = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
      %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      %count_zero = simulation.logic.count_bits %mixed matching %zero : (!simulation.logic<12>, !simulation.logic<1>) -> i32
      %count_all = simulation.logic.count_bits %mixed matching %zero, %one, %x, %z, %one : (!simulation.logic<12>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) -> i32
      %clog2 = simulation.logic.clog2 %mixed : !simulation.logic<12>
      %clog2_zero = simulation.logic.clog2 %zero : !simulation.logic<1>
      simulation.return %count_zero, %count_all, %clog2, %clog2_zero : i32, i32, i32, i32
    }

    simulation.func @conditional_merge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (!simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>) attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
      %left = simulation.logic.constant 11 : i4, 1 : i4 : !simulation.logic<4>
      %right = simulation.logic.constant 9 : i4, 1 : i4 : !simulation.logic<4>
      %symbols_left = simulation.logic.constant 6 : i4, 12 : i4 : !simulation.logic<4>
      %symbols_right = simulation.logic.constant 6 : i4, 12 : i4 : !simulation.logic<4>
      %mismatch = simulation.logic.mux %x ? %left : %right : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
      %matching_symbols = simulation.logic.mux %x ? %symbols_left : %symbols_right : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
      %known = simulation.logic.mux %zero ? %left : %right : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
      simulation.return %mismatch, %matching_symbols, %known : !simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>
    }
  }
}

// CHECK-LABEL: simulation.func @constants
// CHECK: %[[LOGIC:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK: %[[BITS:.*]] = arith.constant 0 : i4
// CHECK: %[[TRUE:.*]] = arith.constant false
// CHECK: simulation.return %[[LOGIC]], %[[BITS]], %[[TRUE]]

// CHECK-LABEL: simulation.func @dynamic_insert_constants
// CHECK-DAG: %[[PARTIAL_LOW:.*]] = simulation.logic.constant -89 : i8, 17 : i8
// CHECK-DAG: %[[PARTIAL_HIGH:.*]] = simulation.logic.constant 37 : i8, 16 : i8
// CHECK-DAG: %[[UNCHANGED_LOGIC:.*]] = simulation.logic.constant -91 : i8, 16 : i8
// CHECK-DAG: %[[PARTIAL_BITS:.*]] = arith.constant -89 : i8
// CHECK-DAG: %[[UNCHANGED_BITS:.*]] = arith.constant -91 : i8
// CHECK-NOT: dyn_insert
// CHECK: simulation.return %[[PARTIAL_LOW]], %[[PARTIAL_HIGH]], %[[UNCHANGED_LOGIC]], %[[PARTIAL_BITS]], %[[UNCHANGED_BITS]]

// CHECK-LABEL: simulation.func @controlling_bitwise
// CHECK: %[[ZERO:.*]] = simulation.logic.constant 0 : i4, 0 : i4
// CHECK: %[[ONES:.*]] = simulation.logic.constant -1 : i4, 0 : i4
// CHECK: %[[AND:.*]] = simulation.logic.binary and %arg1, %[[ONES]]
// CHECK: %[[OR:.*]] = simulation.logic.binary or %arg1, %[[ZERO]]
// CHECK: simulation.return %[[ZERO]], %[[ONES]], %[[AND]], %[[OR]]

// CHECK-LABEL: simulation.func @resize_chains
// CHECK-DAG: %[[COLLAPSED:.*]] = simulation.logic.resize %arg1 signed = true : !simulation.logic<8> -> !simulation.logic<12>
// CHECK-DAG: %[[WIDE:.*]] = simulation.logic.resize %arg1 signed = true : !simulation.logic<8> -> !simulation.logic<16>
// CHECK: %[[PRESERVED:.*]] = simulation.logic.resize %[[WIDE]] signed = false : !simulation.logic<16> -> !simulation.logic<32>
// CHECK: simulation.return %[[COLLAPSED]], %[[PRESERVED]]

// CHECK-LABEL: simulation.func @operand_order
// CHECK: %[[ONE:.*]] = simulation.logic.constant 1 : i4, 0 : i4
// CHECK: %[[SUM:.*]] = simulation.logic.binary add %arg1, %[[ONE]]
// CHECK: %[[CMP:.*]] = simulation.logic.compare ugt %arg1, %[[ONE]]
// CHECK: simulation.return %[[SUM]], %[[CMP]]

// CHECK-LABEL: simulation.func @structural
// CHECK: %[[REPEATED:.*]] = simulation.logic.replicate %arg1 times 2
// CHECK: %[[ADJACENT:.*]] = simulation.logic.extract %arg3 from 0 {{.*}}!simulation.logic<8>
// CHECK: %[[DISJOINT:.*]] = simulation.logic.extract %arg3 from 0
// CHECK: %[[INSIDE:.*]] = simulation.logic.extract %arg5 from 2
// CHECK: %[[SHADOWED:.*]] = simulation.logic.insert %arg5 into %arg3 at 4
// CHECK: %[[DYNAMIC_KNOWN:.*]] = simulation.logic.insert %arg4 into %arg3 at 4
// CHECK: simulation.return %[[REPEATED]], %arg1, %[[ADJACENT]], %arg2, %[[DISJOINT]], %[[INSIDE]], %[[SHADOWED]], %[[DYNAMIC_KNOWN]]

// CHECK-LABEL: simulation.func @matching_fold
// CHECK: %[[MATCH_ONE:.*]] = simulation.logic.constant true, false : !simulation.logic<1>
// CHECK: %[[MATCH_X:.*]] = simulation.logic.constant false, true : !simulation.logic<1>
// CHECK: %[[MATCH_TRUE:.*]] = arith.constant true
// CHECK: %[[MATCH_FALSE:.*]] = arith.constant false
// CHECK-NOT: simulation.logic.compare
// CHECK: simulation.return %[[MATCH_ONE]], %[[MATCH_X]], %[[MATCH_ONE]], %[[MATCH_TRUE]], %[[MATCH_FALSE]], %[[MATCH_TRUE]], %[[MATCH_TRUE]]

// CHECK-LABEL: simulation.func @pure_inquiries
// CHECK-DAG: %[[FOUR:.*]] = arith.constant 4 : i32
// CHECK-DAG: %[[TWELVE:.*]] = arith.constant 12 : i32
// CHECK-DAG: %[[ELEVEN:.*]] = arith.constant 11 : i32
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-NOT: simulation.logic.count_bits
// CHECK-NOT: simulation.logic.clog2
// CHECK: simulation.return %[[FOUR]], %[[TWELVE]], %[[ELEVEN]], %[[ZERO]]

// CHECK-LABEL: simulation.func @conditional_merge
// CHECK-DAG: %[[MISMATCH:.*]] = simulation.logic.constant -8 : i4, 3 : i4
// CHECK-DAG: %[[SYMBOLS:.*]] = simulation.logic.constant 2 : i4, -4 : i4
// CHECK-DAG: %[[KNOWN:.*]] = simulation.logic.constant -7 : i4, 1 : i4
// CHECK-NOT: simulation.logic.mux
// CHECK: simulation.return %[[MISMATCH]], %[[SYMBOLS]], %[[KNOWN]]
