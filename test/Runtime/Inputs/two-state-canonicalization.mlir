// Input for an exhaustive native/Wasm comparison with the ordinary four-state lowering.
func.func @arithmetic(%a: i8, %b: i8) -> i64 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %lb = simulation.logic.from_bits %b : i8 -> !simulation.logic<8>
  %r0 = simulation.logic.binary add %la, %lb : !simulation.logic<8>
  %b0 = simulation.logic.to_bits %r0 : !simulation.logic<8> -> i8
  %w0 = arith.extui %b0 : i8 to i64
  %r1 = simulation.logic.binary sub %la, %lb : !simulation.logic<8>
  %b1 = simulation.logic.to_bits %r1 : !simulation.logic<8> -> i8
  %w1 = arith.extui %b1 : i8 to i64
  %c1 = arith.constant 8 : i64
  %s1 = arith.shli %w1, %c1 : i64
  %v1 = arith.ori %w0, %s1 : i64
  %r2 = simulation.logic.binary mul %la, %lb : !simulation.logic<8>
  %b2 = simulation.logic.to_bits %r2 : !simulation.logic<8> -> i8
  %w2 = arith.extui %b2 : i8 to i64
  %c2 = arith.constant 16 : i64
  %s2 = arith.shli %w2, %c2 : i64
  %v2 = arith.ori %v1, %s2 : i64
  %r3 = simulation.logic.binary and %la, %lb : !simulation.logic<8>
  %b3 = simulation.logic.to_bits %r3 : !simulation.logic<8> -> i8
  %w3 = arith.extui %b3 : i8 to i64
  %c3 = arith.constant 24 : i64
  %s3 = arith.shli %w3, %c3 : i64
  %v3 = arith.ori %v2, %s3 : i64
  %r4 = simulation.logic.binary or %la, %lb : !simulation.logic<8>
  %b4 = simulation.logic.to_bits %r4 : !simulation.logic<8> -> i8
  %w4 = arith.extui %b4 : i8 to i64
  %c4 = arith.constant 32 : i64
  %s4 = arith.shli %w4, %c4 : i64
  %v4 = arith.ori %v3, %s4 : i64
  %r5 = simulation.logic.binary xor %la, %lb : !simulation.logic<8>
  %b5 = simulation.logic.to_bits %r5 : !simulation.logic<8> -> i8
  %w5 = arith.extui %b5 : i8 to i64
  %c5 = arith.constant 40 : i64
  %s5 = arith.shli %w5, %c5 : i64
  %v5 = arith.ori %v4, %s5 : i64
  %r6 = simulation.logic.binary xnor %la, %lb : !simulation.logic<8>
  %b6 = simulation.logic.to_bits %r6 : !simulation.logic<8> -> i8
  %w6 = arith.extui %b6 : i8 to i64
  %c6 = arith.constant 48 : i64
  %s6 = arith.shli %w6, %c6 : i64
  %v6 = arith.ori %v5, %s6 : i64
  return %v6 : i64
}

func.func @comparisons(%a: i8, %b: i8) -> i32 {
  %a65 = arith.extsi %a : i8 to i65
  %b65 = arith.extsi %b : i8 to i65
  %la = simulation.logic.from_bits %a65 : i65 -> !simulation.logic<65>
  %lb = simulation.logic.from_bits %b65 : i65 -> !simulation.logic<65>
  %r0 = simulation.logic.compare eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b0 = simulation.logic.is_true %r0 : !simulation.logic<1>
  %w0 = arith.extui %b0 : i1 to i32
  %r1 = simulation.logic.compare ne %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b1 = simulation.logic.is_true %r1 : !simulation.logic<1>
  %w1 = arith.extui %b1 : i1 to i32
  %c1 = arith.constant 1 : i32
  %s1 = arith.shli %w1, %c1 : i32
  %v1 = arith.ori %w0, %s1 : i32
  %r2 = simulation.logic.compare ult %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b2 = simulation.logic.is_true %r2 : !simulation.logic<1>
  %w2 = arith.extui %b2 : i1 to i32
  %c2 = arith.constant 2 : i32
  %s2 = arith.shli %w2, %c2 : i32
  %v2 = arith.ori %v1, %s2 : i32
  %r3 = simulation.logic.compare ule %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b3 = simulation.logic.is_true %r3 : !simulation.logic<1>
  %w3 = arith.extui %b3 : i1 to i32
  %c3 = arith.constant 3 : i32
  %s3 = arith.shli %w3, %c3 : i32
  %v3 = arith.ori %v2, %s3 : i32
  %r4 = simulation.logic.compare ugt %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b4 = simulation.logic.is_true %r4 : !simulation.logic<1>
  %w4 = arith.extui %b4 : i1 to i32
  %c4 = arith.constant 4 : i32
  %s4 = arith.shli %w4, %c4 : i32
  %v4 = arith.ori %v3, %s4 : i32
  %r5 = simulation.logic.compare uge %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b5 = simulation.logic.is_true %r5 : !simulation.logic<1>
  %w5 = arith.extui %b5 : i1 to i32
  %c5 = arith.constant 5 : i32
  %s5 = arith.shli %w5, %c5 : i32
  %v5 = arith.ori %v4, %s5 : i32
  %r6 = simulation.logic.compare slt %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b6 = simulation.logic.is_true %r6 : !simulation.logic<1>
  %w6 = arith.extui %b6 : i1 to i32
  %c6 = arith.constant 6 : i32
  %s6 = arith.shli %w6, %c6 : i32
  %v6 = arith.ori %v5, %s6 : i32
  %r7 = simulation.logic.compare sle %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b7 = simulation.logic.is_true %r7 : !simulation.logic<1>
  %w7 = arith.extui %b7 : i1 to i32
  %c7 = arith.constant 7 : i32
  %s7 = arith.shli %w7, %c7 : i32
  %v7 = arith.ori %v6, %s7 : i32
  %r8 = simulation.logic.compare sgt %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b8 = simulation.logic.is_true %r8 : !simulation.logic<1>
  %w8 = arith.extui %b8 : i1 to i32
  %c8 = arith.constant 8 : i32
  %s8 = arith.shli %w8, %c8 : i32
  %v8 = arith.ori %v7, %s8 : i32
  %r9 = simulation.logic.compare sge %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b9 = simulation.logic.is_true %r9 : !simulation.logic<1>
  %w9 = arith.extui %b9 : i1 to i32
  %c9 = arith.constant 9 : i32
  %s9 = arith.shli %w9, %c9 : i32
  %v9 = arith.ori %v8, %s9 : i32
  %r10 = simulation.logic.compare case_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  %w10 = arith.extui %r10 : i1 to i32
  %c10 = arith.constant 10 : i32
  %s10 = arith.shli %w10, %c10 : i32
  %v10 = arith.ori %v9, %s10 : i32
  %r11 = simulation.logic.compare case_ne %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  %w11 = arith.extui %r11 : i1 to i32
  %c11 = arith.constant 11 : i32
  %s11 = arith.shli %w11, %c11 : i32
  %v11 = arith.ori %v10, %s11 : i32
  %r12 = simulation.logic.compare casez_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  %w12 = arith.extui %r12 : i1 to i32
  %c12 = arith.constant 12 : i32
  %s12 = arith.shli %w12, %c12 : i32
  %v12 = arith.ori %v11, %s12 : i32
  %r13 = simulation.logic.compare casexz_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> i1
  %w13 = arith.extui %r13 : i1 to i32
  %c13 = arith.constant 13 : i32
  %s13 = arith.shli %w13, %c13 : i32
  %v13 = arith.ori %v12, %s13 : i32
  %r14 = simulation.logic.compare wild_eq %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b14 = simulation.logic.is_true %r14 : !simulation.logic<1>
  %w14 = arith.extui %b14 : i1 to i32
  %c14 = arith.constant 14 : i32
  %s14 = arith.shli %w14, %c14 : i32
  %v14 = arith.ori %v13, %s14 : i32
  %r15 = simulation.logic.compare wild_ne %la, %lb : (!simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<1>
  %b15 = simulation.logic.is_true %r15 : !simulation.logic<1>
  %w15 = arith.extui %b15 : i1 to i32
  %c15 = arith.constant 15 : i32
  %s15 = arith.shli %w15, %c15 : i32
  %v15 = arith.ori %v14, %s15 : i32
  return %v15 : i32
}

// Resize arithmetic follows the resolved expression width, including overflow.
func.func @parameter(%a: i8) -> i64 {
  %la = simulation.logic.from_bits %a : i8 -> !simulation.logic<8>
  %p = simulation.logic.constant 2 : i8, 0 : i8 : !simulation.logic<8>
  %sum = simulation.logic.binary add %la, %p : !simulation.logic<8>
  %wide = simulation.logic.resize %sum signed = true : !simulation.logic<8> -> !simulation.logic<128>
  %narrow = simulation.logic.resize %wide signed = true : !simulation.logic<128> -> !simulation.logic<64>
  %bits = simulation.logic.to_bits %narrow : !simulation.logic<64> -> i64
  return %bits : i64
}
