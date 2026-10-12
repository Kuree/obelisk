// RUN: obelisk-opt %s --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %s --canonicalize --cse -o %t.once
// RUN: obelisk-opt %t.once --canonicalize --cse -o %t.twice
// RUN: diff %t.once %t.twice

// IEEE 1800-2023 11.4.3-11: preserve X/Z, widths, and exceptional arithmetic.
// Every function has a symbolic operand so these test partial/structural folds.

// CHECK-LABEL: func.func @add_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @add_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary add %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @add_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @add_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary add %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @sub_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @sub_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary sub %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @sub_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @sub_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary sub %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @mul_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @mul_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary mul %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @mul_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @mul_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary mul %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @udiv_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @udiv_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary udiv %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @udiv_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @udiv_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary udiv %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @sdiv_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @sdiv_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary sdiv %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @sdiv_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @sdiv_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary sdiv %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @umod_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @umod_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary umod %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @umod_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @umod_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary umod %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @smod_unknown_lhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @smod_unknown_lhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary smod %c, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @smod_unknown_rhs(
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[X]]
func.func @smod_unknown_rhs(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary smod %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @udiv_zero(
// CHECK: simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @udiv_zero(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary udiv %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @sdiv_zero(
// CHECK: simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @sdiv_zero(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary sdiv %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @umod_zero(
// CHECK: simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @umod_zero(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary umod %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @smod_zero(
// CHECK: simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @smod_zero(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %c = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary smod %x, %c : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @combine_add(
// CHECK: %[[C:.*]] = simulation.logic.constant 5 : i4, 0 : i4
// CHECK: %[[R:.*]] = simulation.logic.binary add %arg0, %[[C]]
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[R]]
func.func @combine_add(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary add %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary add %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @combine_mul(
// CHECK: %[[C:.*]] = simulation.logic.constant 6 : i4, 0 : i4
// CHECK: %[[R:.*]] = simulation.logic.binary mul %arg0, %[[C]]
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[R]]
func.func @combine_mul(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary mul %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary mul %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @combine_and(
// CHECK: %[[C:.*]] = simulation.logic.constant 2 : i4, 0 : i4
// CHECK: %[[R:.*]] = simulation.logic.binary and %arg0, %[[C]]
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[R]]
func.func @combine_and(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary and %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary and %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @combine_or(
// CHECK: %[[C:.*]] = simulation.logic.constant 3 : i4, 0 : i4
// CHECK: %[[R:.*]] = simulation.logic.binary or %arg0, %[[C]]
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[R]]
func.func @combine_or(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary or %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary or %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @combine_xor(
// CHECK: %[[C:.*]] = simulation.logic.constant 1 : i4, 0 : i4
// CHECK: %[[R:.*]] = simulation.logic.binary xor %arg0, %[[C]]
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[R]]
func.func @combine_xor(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary xor %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary xor %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @combine_xnor(
// CHECK: %[[C:.*]] = simulation.logic.constant -2 : i4, 0 : i4
// CHECK: %[[R:.*]] = simulation.logic.binary xnor %arg0, %[[C]]
// CHECK-NOT: simulation.logic.binary
// CHECK: return %[[R]]
func.func @combine_xnor(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary xnor %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary xnor %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @no_reassociate_sub(
// CHECK: simulation.logic.binary sub
// CHECK: simulation.logic.binary sub
// CHECK: return
func.func @no_reassociate_sub(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary sub %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary sub %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @no_reassociate_udiv(
// CHECK: simulation.logic.binary udiv
// CHECK: simulation.logic.binary udiv
// CHECK: return
func.func @no_reassociate_udiv(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary udiv %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary udiv %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @no_reassociate_sdiv(
// CHECK: simulation.logic.binary sdiv
// CHECK: simulation.logic.binary sdiv
// CHECK: return
func.func @no_reassociate_sdiv(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary sdiv %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary sdiv %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @no_reassociate_umod(
// CHECK: simulation.logic.binary umod
// CHECK: simulation.logic.binary umod
// CHECK: return
func.func @no_reassociate_umod(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary umod %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary umod %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @no_reassociate_smod(
// CHECK: simulation.logic.binary smod
// CHECK: simulation.logic.binary smod
// CHECK: return
func.func @no_reassociate_smod(%x: !simulation.logic<4>) -> !simulation.logic<4> {
  %a = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %b = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary smod %x, %a : !simulation.logic<4>
  %r = simulation.logic.binary smod %i, %b : !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @known_negate(
// CHECK: arith.subi
// CHECK-NOT: simulation.logic.unary
// CHECK: return
func.func @known_negate(%x: i4) -> !simulation.logic<4> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.unary negate %l : (!simulation.logic<4>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @known_bit_not(
// CHECK: arith.xori
// CHECK-NOT: simulation.logic.unary
// CHECK: return
func.func @known_bit_not(%x: i4) -> !simulation.logic<4> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.unary bit_not %l : (!simulation.logic<4>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @known_logical_not(
// CHECK: arith.cmpi eq
// CHECK-NOT: simulation.logic.unary
// CHECK: return
func.func @known_logical_not(%x: i4) -> !simulation.logic<1> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.unary logical_not %l : (!simulation.logic<4>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @known_reduce_and(
// CHECK: arith.cmpi
// CHECK-NOT: simulation.logic.reduction
// CHECK: return
func.func @known_reduce_and(%x: i4) -> !simulation.logic<1> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.reduction and %l : !simulation.logic<4> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @known_reduce_nand(
// CHECK: arith.cmpi
// CHECK-NOT: simulation.logic.reduction
// CHECK: return
func.func @known_reduce_nand(%x: i4) -> !simulation.logic<1> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.reduction nand %l : !simulation.logic<4> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @known_reduce_or(
// CHECK: arith.cmpi
// CHECK-NOT: simulation.logic.reduction
// CHECK: return
func.func @known_reduce_or(%x: i4) -> !simulation.logic<1> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.reduction or %l : !simulation.logic<4> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @known_reduce_nor(
// CHECK: arith.cmpi
// CHECK-NOT: simulation.logic.reduction
// CHECK: return
func.func @known_reduce_nor(%x: i4) -> !simulation.logic<1> {
  %l = simulation.logic.from_bits %x : i4 -> !simulation.logic<4>
  %r = simulation.logic.reduction nor %l : !simulation.logic<4> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_and_2(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction and %arg0
// CHECK: return
func.func @reduce_replicate_and_2(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r = simulation.logic.reduction and %w : !simulation.logic<8> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_and_3(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction and %arg0
// CHECK: return
func.func @reduce_replicate_and_3(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r = simulation.logic.reduction and %w : !simulation.logic<12> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_or_2(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction or %arg0
// CHECK: return
func.func @reduce_replicate_or_2(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r = simulation.logic.reduction or %w : !simulation.logic<8> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_or_3(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction or %arg0
// CHECK: return
func.func @reduce_replicate_or_3(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r = simulation.logic.reduction or %w : !simulation.logic<12> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_xor_2(
// CHECK: simulation.logic.replicate
// CHECK: simulation.logic.reduction xor
// CHECK: return
func.func @reduce_replicate_xor_2(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r = simulation.logic.reduction xor %w : !simulation.logic<8> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_xor_3(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction xor %arg0
// CHECK: return
func.func @reduce_replicate_xor_3(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r = simulation.logic.reduction xor %w : !simulation.logic<12> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_xnor_2(
// CHECK: simulation.logic.replicate
// CHECK: simulation.logic.reduction xnor
// CHECK: return
func.func @reduce_replicate_xnor_2(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r = simulation.logic.reduction xnor %w : !simulation.logic<8> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_xnor_3(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction xnor %arg0
// CHECK: return
func.func @reduce_replicate_xnor_3(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r = simulation.logic.reduction xnor %w : !simulation.logic<12> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_nand_2(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction nand %arg0
// CHECK: return
func.func @reduce_replicate_nand_2(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r = simulation.logic.reduction nand %w : !simulation.logic<8> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_nand_3(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.reduction nand %arg0
// CHECK: return
func.func @reduce_replicate_nand_3(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r = simulation.logic.reduction nand %w : !simulation.logic<12> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_nor_2(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.unary logical_not %arg0
// CHECK: return
func.func @reduce_replicate_nor_2(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r = simulation.logic.reduction nor %w : !simulation.logic<8> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @reduce_replicate_nor_3(
// CHECK-NOT: simulation.logic.replicate
// CHECK: simulation.logic.unary logical_not %arg0
// CHECK: return
func.func @reduce_replicate_nor_3(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %w = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r = simulation.logic.reduction nor %w : !simulation.logic<12> -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @logical_identity_and(
// CHECK: simulation.logic.reduction or %arg0
// CHECK-NOT: simulation.logic.logical
// CHECK: return
func.func @logical_identity_and(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %c = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
  %r = simulation.logic.logical and %x, %c : (!simulation.logic<4>, !simulation.logic<1>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @logical_self_and(
// CHECK: simulation.logic.reduction or %arg0
// CHECK: return
func.func @logical_self_and(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %r = simulation.logic.logical and %x, %x : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @logical_identity_or(
// CHECK: simulation.logic.reduction or %arg0
// CHECK-NOT: simulation.logic.logical
// CHECK: return
func.func @logical_identity_or(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %c = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
  %r = simulation.logic.logical or %x, %c : (!simulation.logic<4>, !simulation.logic<1>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @logical_self_or(
// CHECK: simulation.logic.reduction or %arg0
// CHECK: return
func.func @logical_self_or(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %r = simulation.logic.logical or %x, %x : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @wild_eq_self(
// CHECK: simulation.logic.constant true, false
// CHECK-NOT: simulation.logic.compare
// CHECK: return
func.func @wild_eq_self(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %r = simulation.logic.compare wild_eq %x, %x : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @wild_ne_self(
// CHECK: simulation.logic.constant false, false
// CHECK-NOT: simulation.logic.compare
// CHECK: return
func.func @wild_ne_self(%x: !simulation.logic<4>) -> !simulation.logic<1> {
  %r = simulation.logic.compare wild_ne %x, %x : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}

// CHECK-LABEL: func.func @known_condition(
// CHECK: arith.select %arg0, %arg1, %arg2
// CHECK-NOT: simulation.logic.mux
// CHECK: return
func.func @known_condition(%c: i1, %a: !simulation.logic<4>, %b: !simulation.logic<4>) -> !simulation.logic<4> {
  %l = simulation.logic.from_bits %c : i1 -> !simulation.logic<1>
  %r = simulation.logic.mux %l ? %a : %b : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @unknown_condition_same_arms(
// CHECK: simulation.logic.mux
// CHECK: return
func.func @unknown_condition_same_arms(%c: !simulation.logic<1>, %a: !simulation.logic<4>) -> !simulation.logic<4> {
  %r = simulation.logic.mux %c ? %a : %a : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @unknown_condition_known_same_arms(
// CHECK: %[[A:.*]] = simulation.logic.from_bits %arg1
// CHECK-NOT: simulation.logic.mux
// CHECK: return %[[A]]
func.func @unknown_condition_known_same_arms(%c: !simulation.logic<1>, %a: i4) -> !simulation.logic<4> {
  %a4 = simulation.logic.from_bits %a : i4 -> !simulation.logic<4>
  %r = simulation.logic.mux %c ? %a4 : %a4 : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}

// CHECK-LABEL: func.func @difference_self(
// CHECK: arith.constant 0 : i4
// CHECK-NOT: simulation.logic.case_difference_mask
// CHECK: return
func.func @difference_self(%a: !simulation.logic<4>) -> i4 {
  %r = simulation.logic.case_difference_mask %a, %a : (!simulation.logic<4>, !simulation.logic<4>) -> i4
  return %r : i4
}

// CHECK-LABEL: func.func @difference_known(
// CHECK: arith.xori %arg0, %arg1
// CHECK-NOT: simulation.logic.case_difference_mask
// CHECK: return
func.func @difference_known(%a: i4, %b: i4) -> i4 {
  %la = simulation.logic.from_bits %a : i4 -> !simulation.logic<4>
  %lb = simulation.logic.from_bits %b : i4 -> !simulation.logic<4>
  %r = simulation.logic.case_difference_mask %la, %lb : (!simulation.logic<4>, !simulation.logic<4>) -> i4
  return %r : i4
}

// CHECK-LABEL: func.func @count_all_states(
// CHECK: arith.constant 4 : i32
// CHECK-NOT: simulation.logic.count_bits
// CHECK: return
func.func @count_all_states(%a: !simulation.logic<4>) -> i32 {
  %z = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
  %o = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
  %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %h = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %r = simulation.logic.count_bits %a matching %z, %o, %x, %h, %x : (!simulation.logic<4>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) -> i32
  return %r : i32
}

// Side effects producing an otherwise unused operand remain observable.
func.func private @effect() -> !simulation.logic<4>
// CHECK-LABEL: func.func @keep_operand_effect
// CHECK: call @effect
// CHECK-NOT: simulation.logic.binary
// CHECK: return
func.func @keep_operand_effect() -> !simulation.logic<4> {
  %a = call @effect() : () -> !simulation.logic<4>
  %x = simulation.logic.constant 0 : i4, 15 : i4 : !simulation.logic<4>
  %r = simulation.logic.binary add %a, %x : !simulation.logic<4>
  return %r : !simulation.logic<4>
}
// A shared inner expression must not be duplicated by reassociation.
// CHECK-LABEL: func.func @shared_inner
// CHECK: %[[INNER:.*]] = simulation.logic.binary and %arg0,
// CHECK: %[[OUTER:.*]] = simulation.logic.binary and %[[INNER]],
// CHECK: return %[[INNER]], %[[OUTER]]
func.func @shared_inner(%x: !simulation.logic<4>) -> (!simulation.logic<4>, !simulation.logic<4>) {
  %c = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %d = simulation.logic.constant 6 : i4, 0 : i4 : !simulation.logic<4>
  %i = simulation.logic.binary and %x, %c : !simulation.logic<4>
  %r = simulation.logic.binary and %i, %d : !simulation.logic<4>
  return %i, %r : !simulation.logic<4>, !simulation.logic<4>
}

// CHECK-LABEL: func.func @known_negate_1
// CHECK: arith.subi {{.*}} : i1
// CHECK-NOT: simulation.logic.unary
// CHECK: return
func.func @known_negate_1(%a: i1) -> i1 {
  %v = simulation.logic.from_bits %a : i1 -> !simulation.logic<1>
  %n = simulation.logic.unary negate %v : (!simulation.logic<1>) -> !simulation.logic<1>
  %r = simulation.logic.to_bits %n : !simulation.logic<1> -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @known_negate_65
// CHECK: arith.subi {{.*}} : i65
// CHECK-NOT: simulation.logic.unary
// CHECK: return
func.func @known_negate_65(%a: i65) -> i65 {
  %v = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %n = simulation.logic.unary negate %v : (!simulation.logic<65>) -> !simulation.logic<65>
  %r = simulation.logic.to_bits %n : !simulation.logic<65> -> i65
  return %r : i65
}

// CHECK-LABEL: func.func @known_negate_128
// CHECK: arith.subi {{.*}} : i128
// CHECK-NOT: simulation.logic.unary
// CHECK: return
func.func @known_negate_128(%a: i128) -> i128 {
  %v = simulation.logic.from_bits %a : i128 -> !simulation.logic<128>
  %n = simulation.logic.unary negate %v : (!simulation.logic<128>) -> !simulation.logic<128>
  %r = simulation.logic.to_bits %n : !simulation.logic<128> -> i128
  return %r : i128
}

// Power has the same whole-result unknown propagation as other arithmetic.
// CHECK-LABEL: func.func @power_unknown_base
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.power
// CHECK: return %[[X]]
func.func @power_unknown_base(%e: !simulation.logic<65>) -> !simulation.logic<4> {
  %x = simulation.logic.constant 8 : i4, 8 : i4 : !simulation.logic<4>
  %r = simulation.logic.power %x, %e : (!simulation.logic<4>, !simulation.logic<65>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}
// CHECK-LABEL: func.func @power_unknown_exponent
// CHECK: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
// CHECK-NOT: simulation.logic.power
// CHECK: return %[[X]]
func.func @power_unknown_exponent(%a: !simulation.logic<4>) -> !simulation.logic<4> {
  %x = simulation.logic.constant 1 : i65, 1 : i65 : !simulation.logic<65>
  %r = simulation.logic.power %a, %x : (!simulation.logic<4>, !simulation.logic<65>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}
// A shift amount retains all its bits when stripping a two-state wrapper.
// CHECK-LABEL: func.func @known_shift_amount
// CHECK-NOT: simulation.logic.from_bits
// CHECK: simulation.logic.shift right_arith %arg0 by %arg1 : (!simulation.logic<4>, i128)
// CHECK: return
func.func @known_shift_amount(%a: !simulation.logic<4>, %amount: i128) -> !simulation.logic<4> {
  %l = simulation.logic.from_bits %amount : i128 -> !simulation.logic<128>
  %r = simulation.logic.shift right_arith %a by %l : (!simulation.logic<4>, !simulation.logic<128>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}
// Only X-to-Z changes here, so to_bits equality would be incorrect.
// CHECK-LABEL: func.func @difference_unknown_symbols
// CHECK: %[[R:.*]] = arith.constant 4 : i4
// CHECK-NOT: simulation.logic.case_difference_mask
// CHECK: return %[[R]]
func.func @difference_unknown_symbols() -> i4 {
  %x = simulation.logic.constant 0 : i4, 4 : i4 : !simulation.logic<4>
  %z = simulation.logic.constant 4 : i4, 4 : i4 : !simulation.logic<4>
  %r = simulation.logic.case_difference_mask %x, %z : (!simulation.logic<4>, !simulation.logic<4>) -> i4
  return %r : i4
}
// CHECK-LABEL: func.func @count_integer
// CHECK: %[[R:.*]] = arith.constant 3 : i32
// CHECK-NOT: simulation.logic.count_bits
// CHECK: return %[[R]]
func.func @count_integer() -> i32 {
  %a = arith.constant 7 : i8
  %one = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
  %r = simulation.logic.count_bits %a matching %one, %one : (i8, !simulation.logic<1>, !simulation.logic<1>) -> i32
  return %r : i32
}
// Unpacked storage padding must not be treated as language bitstream bits.
// CHECK-LABEL: func.func @count_unpacked
// CHECK: simulation.logic.count_bits
// CHECK: return
func.func @count_unpacked(%a: !simulation.unpacked_array<0 : 1 x i3>) -> i32 {
  %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
  %one = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
  %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %r = simulation.logic.count_bits %a matching %zero, %one, %x, %z : (!simulation.unpacked_array<0 : 1 x i3>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) -> i32
  return %r : i32
}
// CHECK-LABEL: func.func @known_logical_or
// CHECK: arith.cmpi ne
// CHECK: arith.cmpi ne
// CHECK: arith.ori
// CHECK-NOT: simulation.logic.logical
// CHECK: return
func.func @known_logical_or(%a: i65, %b: i7) -> !simulation.logic<1> {
  %la = simulation.logic.from_bits %a : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b : i7 -> !simulation.logic<7>
  %r = simulation.logic.logical or %la, %lb : (!simulation.logic<65>, !simulation.logic<7>) -> !simulation.logic<1>
  return %r : !simulation.logic<1>
}
// Explicitly signed builtin results cannot be rewritten to signless arith ops.
// CHECK-LABEL: func.func @signed_difference_result
// CHECK: simulation.logic.case_difference_mask
// CHECK: return
func.func @signed_difference_result(%a: i4, %b: i4) -> si4 {
  %la = simulation.logic.from_bits %a : i4 -> !simulation.logic<4>
  %lb = simulation.logic.from_bits %b : i4 -> !simulation.logic<4>
  %r = simulation.logic.case_difference_mask %la, %lb : (!simulation.logic<4>, !simulation.logic<4>) -> si4
  return %r : si4
}
// CHECK-LABEL: func.func @signed_string_result
// CHECK: simulation.string.to_packed
// CHECK: return
func.func @signed_string_result() -> si8 {
  %s = simulation.string.literal "A"
  %r = simulation.string.to_packed %s : (!simulation.string) -> si8
  return %r : si8
}
