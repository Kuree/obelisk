// RUN: obelisk-opt %s --canonicalize | FileCheck %s
// RUN: obelisk-opt %s --canonicalize="top-down=false" | FileCheck %s
// RUN: obelisk-opt %s --canonicalize -o %t.once
// RUN: obelisk-opt %t.once --canonicalize -o %t.twice
// RUN: diff %t.once %t.twice

// Canonical spellings of IEEE 1800-2023 operators that agree on every 0, 1, X
// and Z input. test/Runtime/simulation-op-canonicalization-forms.test checks
// the same rules exhaustively against the reference lowering.
!l1 = !simulation.logic<1>
!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>

// IEEE 1800-2023 11.6.1: an identity or round-trip resize does not change the
// signedness of a later sign extension (the inner unsigned resize used to win).
// CHECK-LABEL: func.func @resize_chain(
// CHECK-NEXT: %[[S0:.*]] = simulation.logic.resize %arg0 signed = true : !simulation.logic<4> -> !simulation.logic<8>
// CHECK-NEXT: %[[S1:.*]] = simulation.logic.resize %arg0 signed = true : !simulation.logic<4> -> !simulation.logic<8>
// CHECK-NEXT: return %[[S0]], %[[S1]]
func.func @resize_chain(%x: !l4) -> (!l8, !l8) {
  %a = simulation.logic.resize %x signed = false : !l4 -> !l8
  %t = simulation.logic.resize %a signed = false : !l8 -> !l4
  %r0 = simulation.logic.resize %t signed = true : !l4 -> !l8
  %same = simulation.logic.resize %x signed = false : !l4 -> !l4
  %r1 = simulation.logic.resize %same signed = true : !l4 -> !l8
  return %r0, %r1 : !l8, !l8
}

// 12.4, 11.4.5, 11.4.7, 11.4.9: truth tests become two-state predicates. A
// multi-bit a & b has no per-operand split, and x == c with X is never true.
// CHECK-LABEL: func.func @truth(
// CHECK-DAG: %[[ONES:.*]] = simulation.logic.constant -1 : i4, 0 : i4
// CHECK-DAG: %[[FALSE:.*]] = arith.constant false
// CHECK-DAG: %[[ZERO:.*]] = simulation.logic.constant 0 : i4, 0 : i4
// CHECK-DAG: %[[FIVE:.*]] = simulation.logic.constant 5 : i4, 0 : i4
// CHECK-DAG: %[[A0:.*]] = simulation.logic.is_true %arg0
// CHECK-DAG: %[[B0:.*]] = simulation.logic.is_true %arg1
// CHECK: %[[AND:.*]] = arith.andi %[[A0]], %[[B0]] : i1
// CHECK-DAG: %[[A1:.*]] = simulation.logic.is_true %arg0
// CHECK-DAG: %[[B1:.*]] = simulation.logic.is_true %arg1
// CHECK: %[[OR:.*]] = arith.ori %[[A1]], %[[B1]] : i1
// CHECK-DAG: %[[A2:.*]] = simulation.logic.is_true %arg2
// CHECK-DAG: %[[B2:.*]] = simulation.logic.is_true %arg3
// CHECK: %[[AND1:.*]] = arith.andi %[[A2]], %[[B2]] : i1
// CHECK: %[[NOT:.*]] = simulation.logic.compare case_eq %arg0, %[[ZERO]]
// CHECK: %[[EQ:.*]] = simulation.logic.compare case_eq %arg0, %[[FIVE]]
// CHECK: %[[ALL:.*]] = simulation.logic.compare case_eq %arg0, %[[ONES]]
// CHECK: %[[REP:.*]] = simulation.logic.compare case_eq %arg0, %[[ZERO]]
// CHECK: %[[WIDE:.*]] = simulation.logic.binary and %arg0, %arg1
// CHECK: %[[KEEP:.*]] = simulation.logic.is_true %[[WIDE]]
// CHECK: return %[[AND]], %[[OR]], %[[AND1]], %[[NOT]], %[[EQ]], %[[FALSE]], %[[ALL]], %[[REP]], %[[KEEP]]
func.func @truth(%a: !l4, %b: !l4, %a1: !l1, %b1: !l1) -> (i1, i1, i1, i1, i1, i1, i1, i1, i1) {
  %land = simulation.logic.logical and %a, %b : (!l4, !l4) -> !l1
  %t0 = simulation.logic.is_true %land : !l1
  %bor = simulation.logic.binary or %a, %b : !l4
  %t1 = simulation.logic.is_true %bor : !l4
  %band = simulation.logic.binary and %a1, %b1 : !l1
  %t2 = simulation.logic.is_true %band : !l1
  %not = simulation.logic.unary logical_not %a : (!l4) -> !l1
  %t3 = simulation.logic.is_true %not : !l1
  %c5 = simulation.logic.constant 5 : i4, 0 : i4 : !l4
  %eq = simulation.logic.compare eq %a, %c5 : (!l4, !l4) -> !l1
  %t4 = simulation.logic.is_true %eq : !l1
  %cx = simulation.logic.constant 5 : i4, 2 : i4 : !l4
  %eqx = simulation.logic.compare eq %a, %cx : (!l4, !l4) -> !l1
  %t5 = simulation.logic.is_true %eqx : !l1
  %rand = simulation.logic.reduction and %a : !l4 -> !l1
  %t6 = simulation.logic.is_true %rand : !l1
  %rep = simulation.logic.replicate %a times 2 : !l4 -> !l8
  %nrep = simulation.logic.unary logical_not %rep : (!l8) -> !l1
  %t7 = simulation.logic.is_true %nrep : !l1
  %band4 = simulation.logic.binary and %a, %b : !l4
  %t8 = simulation.logic.is_true %band4 : !l4
  return %t0, %t1, %t2, %t3, %t4, %t5, %t6, %t7, %t8 : i1, i1, i1, i1, i1, i1, i1, i1, i1
}

// 11.4.4-7, 11.4.9, Table 11-15: x == 0 is !x, x != 0 is |x, and negating a
// 0/1/X comparison or reduction selects the inverse operator. A one-bit
// negation is spelled as the type-preserving bitwise negation.
// CHECK-LABEL: func.func @negations(
// CHECK-NEXT: %[[EQ:.*]] = simulation.logic.unary logical_not %arg0
// CHECK-NEXT: %[[NE:.*]] = simulation.logic.reduction or %arg0
// CHECK-NEXT: %[[GE:.*]] = simulation.logic.compare uge %arg0, %arg1
// CHECK-NEXT: %[[WN:.*]] = simulation.logic.compare wild_ne %arg0, %arg1
// CHECK-NEXT: %[[NN:.*]] = simulation.logic.reduction or %arg0
// CHECK-NEXT: %[[NAND:.*]] = simulation.logic.reduction nand %arg0
// CHECK-NEXT: %[[BIT:.*]] = simulation.logic.unary bit_not %arg2 : (!simulation.logic<1>)
// CHECK-NEXT: return %[[EQ]], %[[NE]], %[[GE]], %[[WN]], %[[NN]], %[[NAND]], %[[BIT]]
func.func @negations(%a: !l4, %b: !l4, %a1: !l1) -> (!l1, !l1, !l1, !l1, !l1, !l1, !l1) {
  %z = simulation.logic.constant 0 : i4, 0 : i4 : !l4
  %eq = simulation.logic.compare eq %a, %z : (!l4, !l4) -> !l1
  %ne = simulation.logic.compare ne %a, %z : (!l4, !l4) -> !l1
  %lt = simulation.logic.compare ult %a, %b : (!l4, !l4) -> !l1
  %nlt = simulation.logic.unary logical_not %lt : (!l1) -> !l1
  %w = simulation.logic.compare wild_eq %a, %b : (!l4, !l4) -> !l1
  %nw = simulation.logic.unary logical_not %w : (!l1) -> !l1
  %n1 = simulation.logic.unary logical_not %a : (!l4) -> !l1
  %nn = simulation.logic.unary logical_not %n1 : (!l1) -> !l1
  %r = simulation.logic.reduction and %a : !l4 -> !l1
  %nr = simulation.logic.unary logical_not %r : (!l1) -> !l1
  %bn = simulation.logic.unary bit_not %a1 : (!l1) -> !l1
  return %eq, %ne, %nlt, %nw, %nn, %nr, %bn : !l1, !l1, !l1, !l1, !l1, !l1, !l1
}

// One-bit reductions normalize Z to X; on a value that cannot hold Z they are
// the identity. One-bit logical connectives are the bitwise ones.
// CHECK-LABEL: func.func @single_bit(
// CHECK-NEXT: %[[XOR:.*]] = simulation.logic.reduction or %arg0
// CHECK-NEXT: %[[NAND:.*]] = simulation.logic.unary bit_not %arg0
// CHECK-NEXT: %[[LT:.*]] = simulation.logic.compare ult %arg2, %arg3
// CHECK-NEXT: %[[OR:.*]] = simulation.logic.binary or %arg0, %arg1
// CHECK-NEXT: %[[NOR:.*]] = simulation.logic.unary logical_not %arg2
// CHECK-NEXT: return %[[XOR]], %[[NAND]], %[[LT]], %[[OR]], %[[NOR]]
func.func @single_bit(%a1: !l1, %b1: !l1, %a: !l4, %b: !l4) -> (!l1, !l1, !l1, !l1, !l1) {
  %x = simulation.logic.reduction xor %a1 : !l1 -> !l1
  %n = simulation.logic.reduction nand %a1 : !l1 -> !l1
  %lt = simulation.logic.compare ult %a, %b : (!l4, !l4) -> !l1
  %id = simulation.logic.reduction and %lt : !l1 -> !l1
  %l = simulation.logic.logical or %a1, %b1 : (!l1, !l1) -> !l1
  %nor = simulation.logic.reduction nor %a : !l4 -> !l1
  return %x, %n, %id, %l, %nor : !l1, !l1, !l1, !l1, !l1
}

// Table 11-20: a negated condition swaps the arms; constant one-bit arms are a
// truth test; equal arms are returned only when they cannot hold Z.
// CHECK-LABEL: func.func @mux(
// CHECK-NEXT: %[[M0:.*]] = simulation.logic.mux %arg0 ? %arg2 : %arg1
// CHECK-NEXT: %[[T:.*]] = simulation.logic.reduction or %arg1
// CHECK-NEXT: %[[M1:.*]] = simulation.logic.mux %[[T]] ? %arg2 : %arg1
// CHECK-NEXT: %[[M2:.*]] = simulation.logic.reduction or %arg0
// CHECK-NEXT: %[[M3:.*]] = simulation.logic.unary bit_not %arg0
// CHECK-NEXT: %[[AND:.*]] = simulation.logic.binary and %arg1, %arg2
// CHECK-NEXT: %[[M5:.*]] = simulation.logic.mux %arg0 ? %arg1 : %arg1
// CHECK-NEXT: return %[[M0]], %[[M1]], %[[M2]], %[[M3]], %[[AND]], %[[M5]]
func.func @mux(%c: !l1, %a: !l4, %b: !l4) -> (!l4, !l4, !l1, !l1, !l4, !l4) {
  %nc = simulation.logic.unary logical_not %c : (!l1) -> !l1
  %m0 = simulation.logic.mux %nc ? %a : %b : (!l1, !l4, !l4) -> !l4
  %na = simulation.logic.unary logical_not %a : (!l4) -> !l1
  %m1 = simulation.logic.mux %na ? %a : %b : (!l1, !l4, !l4) -> !l4
  %one = simulation.logic.constant 1 : i1, 0 : i1 : !l1
  %zero = simulation.logic.constant 0 : i1, 0 : i1 : !l1
  %m2 = simulation.logic.mux %c ? %one : %zero : (!l1, !l1, !l1) -> !l1
  %m3 = simulation.logic.mux %c ? %zero : %one : (!l1, !l1, !l1) -> !l1
  %and = simulation.logic.binary and %a, %b : !l4
  %m4 = simulation.logic.mux %c ? %and : %and : (!l1, !l4, !l4) -> !l4
  %m5 = simulation.logic.mux %c ? %a : %a : (!l1, !l4, !l4) -> !l4
  return %m0, %m1, %m2, %m3, %m4, %m5 : !l4, !l4, !l1, !l1, !l4, !l4
}

// x ^ '1 is ~x. Other identities hold only for values that cannot hold Z
// (bitwise) or whole-value arithmetic results (11.4.3); an arbitrary
// four-state operand keeps the operation.
// CHECK-LABEL: func.func @identities(
// CHECK: %[[NOT:.*]] = simulation.logic.unary bit_not %arg0
// CHECK: %[[AND:.*]] = simulation.logic.binary and %arg0, %arg1
// CHECK: %[[OR:.*]] = simulation.logic.binary or %arg0, %{{.*}}
// CHECK: %[[SELF:.*]] = simulation.logic.binary and %arg0, %arg0
// CHECK: %[[BIT:.*]] = simulation.logic.reduction or %arg2
// CHECK: %[[SUM:.*]] = simulation.logic.binary add %arg0, %arg1
// CHECK: %[[ADD0:.*]] = simulation.logic.binary add %arg0, %{{.*}}
// CHECK: %[[N0:.*]] = simulation.logic.unary negate %arg0
// CHECK: %[[N1:.*]] = simulation.logic.unary negate %[[N0]]
// CHECK: %[[B0:.*]] = simulation.logic.unary bit_not %arg0
// CHECK: %[[B1:.*]] = simulation.logic.unary bit_not %[[B0]]
// CHECK: return %[[NOT]], %[[AND]], %[[OR]], %[[SELF]], %[[BIT]], %[[SUM]], %[[ADD0]], %[[SUM]], %[[N1]], %[[B1]]
func.func @identities(%a: !l4, %b: !l4, %a1: !l1) -> (!l4, !l4, !l4, !l4, !l1, !l4, !l4, !l4, !l4, !l4) {
  %ones = simulation.logic.constant 15 : i4, 0 : i4 : !l4
  %zero = simulation.logic.constant 0 : i4, 0 : i4 : !l4
  %r0 = simulation.logic.binary xor %a, %ones : !l4
  %and = simulation.logic.binary and %a, %b : !l4
  %r1 = simulation.logic.binary or %and, %zero : !l4
  %r2 = simulation.logic.binary or %a, %zero : !l4
  %r3 = simulation.logic.binary and %a, %a : !l4
  %one1 = simulation.logic.constant 1 : i1, 0 : i1 : !l1
  %r4 = simulation.logic.binary and %a1, %one1 : !l1
  %sum = simulation.logic.binary add %a, %b : !l4
  %r5 = simulation.logic.binary add %sum, %zero : !l4
  %r6 = simulation.logic.binary add %a, %zero : !l4
  %n0 = simulation.logic.unary negate %sum : (!l4) -> !l4
  %r7 = simulation.logic.unary negate %n0 : (!l4) -> !l4
  %n1 = simulation.logic.unary negate %a : (!l4) -> !l4
  %r8 = simulation.logic.unary negate %n1 : (!l4) -> !l4
  %b0 = simulation.logic.unary bit_not %a : (!l4) -> !l4
  %r9 = simulation.logic.unary bit_not %b0 : (!l4) -> !l4
  return %r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7, %r8, %r9 : !l4, !l4, !l4, !l4, !l1, !l4, !l4, !l4, !l4, !l4
}

// 11.4.10, 11.4.12, 11.5.1, 11.6.1: truncation is a low slice, {'0, x} is a
// zero extension, slices see through extensions and constant shifts, and a
// partially overlapping constant selection reads X or writes only in range.
// A bitwise slice is narrowed only when its operand slices fold.
// CHECK-LABEL: func.func @selections(
// CHECK-DAG: %[[X:.*]] = simulation.logic.constant false, true : !simulation.logic<1>
// CHECK: %[[TRUNC:.*]] = simulation.logic.extract %arg0 from 0 : !simulation.logic<4> -> !simulation.logic<2>
// CHECK: %[[ZEXT:.*]] = simulation.logic.resize %arg0 signed = false : !simulation.logic<4> -> !simulation.logic<8>
// CHECK: %[[HIGH:.*]] = simulation.logic.extract %arg0 from 2 : !simulation.logic<4> -> !simulation.logic<2>
// CHECK: %[[MIXED:.*]] = simulation.logic.resize %[[HIGH]] signed = true : !simulation.logic<2> -> !simulation.logic<4>
// CHECK: %[[SIGN:.*]] = simulation.logic.extract %arg0 from 3 : !simulation.logic<4> -> !simulation.logic<1>
// CHECK: %[[FILL:.*]] = simulation.logic.replicate %[[SIGN]] times 4
// CHECK: %[[TOP:.*]] = simulation.logic.extract %arg2 from 7 : !simulation.logic<8> -> !simulation.logic<1>
// CHECK: %[[SHIFT:.*]] = simulation.logic.resize %[[TOP]] signed = true : !simulation.logic<1> -> !simulation.logic<4>
// CHECK: %[[AND:.*]] = simulation.logic.binary and %arg2, %{{.*}} : !simulation.logic<8>
// CHECK: %[[NARROW:.*]] = simulation.logic.extract %[[AND]] from 2
// CHECK: %[[LOW:.*]] = simulation.logic.extract %arg2 from 0 : !simulation.logic<8> -> !simulation.logic<3>
// CHECK: %[[READ:.*]] = simulation.logic.concat %[[LOW]], %[[X]]
// CHECK: %[[PIECE:.*]] = simulation.logic.extract %arg1 from 0 : !simulation.logic<4> -> !simulation.logic<2>
// CHECK: %[[WRITE:.*]] = simulation.logic.insert %[[PIECE]] into %arg2 at 6
// CHECK: return %[[TRUNC]], %[[ZEXT]], %[[MIXED]], %[[FILL]], %[[SHIFT]], %[[NARROW]], %[[READ]], %[[WRITE]]
func.func @selections(%x: !l4, %y: !l4, %xy: !l8) -> (!simulation.logic<2>, !l8, !l4, !l4, !l4, !l4, !l4, !l8) {
  %t = simulation.logic.resize %x signed = true : !l4 -> !simulation.logic<2>
  %z = simulation.logic.constant 0 : i4, 0 : i4 : !l4
  %zx = simulation.logic.concat %z, %x : (!l4, !l4) -> !l8
  %s = simulation.logic.resize %x signed = true : !l4 -> !l8
  %e0 = simulation.logic.extract %s from 2 : !l8 -> !l4
  %e1 = simulation.logic.extract %s from 4 : !l8 -> !l4
  %k = arith.constant 3 : i32
  %sh = simulation.logic.shift right_arith %xy by %k : (!l8, i32) -> !l8
  %e2 = simulation.logic.extract %sh from 4 : !l8 -> !l4
  %m = simulation.logic.constant 165 : i8, 0 : i8 : !l8
  %and = simulation.logic.binary and %xy, %m : !l8
  %e3 = simulation.logic.extract %and from 2 : !l8 -> !l4
  %neg = arith.constant -1 : i32
  %e4 = simulation.logic.dyn_extract %xy from %neg : (!l8, i32) -> !l4
  %i6 = arith.constant 6 : i32
  %w = simulation.logic.dyn_insert %y into %xy at %i6 : (!l8, !l4, i32) -> !l8
  return %t, %zx, %e0, %e1, %e2, %e3, %e4, %w : !simulation.logic<2>, !l8, !l4, !l4, !l4, !l4, !l4, !l8
}

// Known operands: constant shifts and constant nonzero divisors become arith
// (11.4.3, 11.4.10; -1 negates to keep MIN / -1 wrapping), parity and
// constant-control counts use ctpop (20.9), and to_bits moves through a
// conjunction with a known operand. A variable divisor and X/Z controls stay.
// CHECK-LABEL: func.func @two_state(
// CHECK-DAG: %[[ZERO8:.*]] = simulation.logic.constant 0 : i8, 0 : i8
// CHECK-DAG: %[[C0:.*]] = arith.constant 0 : i8
// CHECK-DAG: %[[C3:.*]] = arith.constant 3 : i8
// CHECK-DAG: %[[C1:.*]] = arith.constant 1 : i8
// CHECK: %[[SHL:.*]] = arith.shli %arg0, %[[C1]] : i8
// CHECK: %[[DIV:.*]] = arith.divui %arg0, %[[C3]] : i8
// CHECK: %[[NEG:.*]] = arith.subi %[[C0]], %arg0 : i8
// CHECK: simulation.logic.binary udiv
// CHECK: %[[POP:.*]] = math.ctpop %arg0 : i8
// CHECK: arith.trunci %[[POP]] : i8 to i1
// CHECK: %[[ONES:.*]] = math.ctpop %arg0 : i8
// CHECK: arith.extui %[[ONES]] : i8 to i32
// CHECK: %[[INV:.*]] = simulation.logic.unary bit_not %arg2
// CHECK: %[[INVBITS:.*]] = simulation.logic.to_bits %[[INV]]
// CHECK: math.ctpop %[[INVBITS]] : i8
// CHECK: simulation.logic.count_bits %arg2 matching
// CHECK: %[[BITS:.*]] = simulation.logic.to_bits %arg2
// CHECK: %[[KNOWN:.*]] = arith.andi %arg0, %[[BITS]] : i8
// CHECK: %[[INS:.*]] = simulation.bits.dyn_insert %arg1 into %arg0 at %arg3
// CHECK: %[[LOW:.*]] = arith.trunci %arg0 : i8 to i2
// CHECK-NOT: simulation.logic.shift
// CHECK: return {{.*}}%[[ZERO8]]{{.*}}%[[KNOWN]]
func.func @two_state(%a: i8, %b: i8, %x: !l8, %i: i3) -> (!l8, !l8, !l8, !l8, !l8, !l1, i32, i32, i32, i8, !l8, !simulation.logic<2>) {
  %k = simulation.logic.from_bits %a : i8 -> !l8
  %c1 = arith.constant 1 : i32
  %r0 = simulation.logic.shift left %k by %c1 : (!l8, i32) -> !l8
  %c3 = simulation.logic.constant 3 : i8, 0 : i8 : !l8
  %r1 = simulation.logic.binary udiv %k, %c3 : !l8
  %m1 = simulation.logic.constant -1 : i8, 0 : i8 : !l8
  %r2 = simulation.logic.binary sdiv %k, %m1 : !l8
  %r3 = simulation.logic.binary smod %k, %m1 : !l8
  %kb = simulation.logic.from_bits %b : i8 -> !l8
  %r4 = simulation.logic.binary udiv %k, %kb : !l8
  %r5 = simulation.logic.reduction xor %k : !l8 -> !l1
  %one = simulation.logic.constant 1 : i1, 0 : i1 : !l1
  %zero = simulation.logic.constant 0 : i1, 0 : i1 : !l1
  %xc = simulation.logic.constant 0 : i1, 1 : i1 : !l1
  %r6 = simulation.logic.count_bits %k matching %one : (!l8, !l1) -> i32
  %r7 = simulation.logic.count_bits %x matching %zero : (!l8, !l1) -> i32
  %r8 = simulation.logic.count_bits %x matching %xc : (!l8, !l1) -> i32
  %and = simulation.logic.binary and %k, %x : !l8
  %r9 = simulation.logic.to_bits %and : !l8 -> i8
  %piece = simulation.logic.from_bits %b : i8 -> !l8
  %r10 = simulation.logic.dyn_insert %piece into %k at %i : (!l8, !l8, i3) -> !l8
  %r11 = simulation.logic.extract %k from 0 : !l8 -> !simulation.logic<2>
  return %r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7, %r8, %r9, %r10, %r11 : !l8, !l8, !l8, !l8, !l8, !l1, i32, i32, i32, i8, !l8, !simulation.logic<2>
}

// A variable shift amount would need a range guard costing more than the
// shift, so a known value shifted by an unknown amount stays a logic shift.
// CHECK-LABEL: func.func @dynamic_shift(
// CHECK: simulation.logic.shift left
func.func @dynamic_shift(%a: i8, %n: i32) -> !l8 {
  %k = simulation.logic.from_bits %a : i8 -> !l8
  %r = simulation.logic.shift left %k by %n : (!l8, i32) -> !l8
  return %r : !l8
}

// A connective with another use stays: splitting its truth test would repeat
// the four-state work. The one-for-one x === c rewrite still applies.
// CHECK-LABEL: func.func @shared_condition(
// CHECK-DAG: %[[FIVE:.*]] = simulation.logic.constant 5 : i4, 0 : i4
// CHECK: %[[AND:.*]] = simulation.logic.logical and %arg0, %arg1
// CHECK: %[[T0:.*]] = simulation.logic.is_true %[[AND]]
// CHECK: %[[EQ:.*]] = simulation.logic.compare eq %arg0, %[[FIVE]]
// CHECK: %[[T1:.*]] = simulation.logic.compare case_eq %arg0, %[[FIVE]]
// CHECK: return %[[T0]], %[[AND]], %[[T1]], %[[EQ]]
func.func @shared_condition(%a: !l4, %b: !l4) -> (i1, !l1, i1, !l1) {
  %land = simulation.logic.logical and %a, %b : (!l4, !l4) -> !l1
  %t0 = simulation.logic.is_true %land : !l1
  %c5 = simulation.logic.constant 5 : i4, 0 : i4 : !l4
  %eq = simulation.logic.compare eq %a, %c5 : (!l4, !l4) -> !l1
  %t1 = simulation.logic.is_true %eq : !l1
  return %t0, %land, %t1, %eq : i1, !l1, i1, !l1
}

// 20.9: counting both known values of a four-state input would take two
// conversions and two counts, so it stays one count_bits.
// CHECK-LABEL: func.func @count_known(
// CHECK: simulation.logic.count_bits %arg0 matching
// CHECK-NOT: math.ctpop
func.func @count_known(%x: !l8) -> i32 {
  %zero = simulation.logic.constant 0 : i1, 0 : i1 : !l1
  %one = simulation.logic.constant 1 : i1, 0 : i1 : !l1
  %r = simulation.logic.count_bits %x matching %zero, %one : (!l8, !l1, !l1) -> i32
  return %r : i32
}
