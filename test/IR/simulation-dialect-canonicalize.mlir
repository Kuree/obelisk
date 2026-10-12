// RUN: obelisk-opt %s --canonicalize -o %t
// RUN: FileCheck %s < %t
// RUN: obelisk-opt %t --canonicalize -o %t.twice
// RUN: diff %t %t.twice

// Dialect patterns must compose across multiple producers and converge.
// CHECK-LABEL: func.func @truth_chain
// CHECK-NOT: simulation.logic.replicate
// CHECK-NOT: simulation.logic.resize
// CHECK-NOT: simulation.logic.reduction
// CHECK: %[[TRUTH:.*]] = simulation.logic.is_true %arg0
// CHECK: return %[[TRUTH]]
func.func @truth_chain(%x: !simulation.logic<4>) -> i1 {
  %repeat = simulation.logic.replicate %x times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %wide = simulation.logic.resize %repeat signed = true : !simulation.logic<12> -> !simulation.logic<65>
  %reduce = simulation.logic.reduction or %wide : !simulation.logic<65> -> !simulation.logic<1>
  %truth = simulation.logic.is_true %reduce : !simulation.logic<1>
  return %truth : i1
}

// Truncating can discard the only known one; other reductions have different
// truth tables. Neither operation can be bypassed by a truth check. A
// truncating resize is the low slice, and ~|x is true only for x === '0.
// CHECK-LABEL: func.func @truth_refusals
// CHECK-DAG: %[[ZERO:.*]] = simulation.logic.constant 0 : i8, 0 : i8
// CHECK-DAG: %[[NARROW:.*]] = simulation.logic.extract %arg0 from 0 : !simulation.logic<8> -> !simulation.logic<4>
// CHECK-DAG: %[[XOR:.*]] = simulation.logic.reduction xor %arg0
// CHECK-DAG: simulation.logic.is_true %[[NARROW]]
// CHECK-DAG: simulation.logic.is_true %[[XOR]]
// CHECK-DAG: simulation.logic.compare case_eq %arg0, %[[ZERO]]
func.func @truth_refusals(%x: !simulation.logic<8>) -> (i1, i1, i1) {
  %narrow = simulation.logic.resize %x signed = false : !simulation.logic<8> -> !simulation.logic<4>
  %xor = simulation.logic.reduction xor %x : !simulation.logic<8> -> !simulation.logic<1>
  %nor = simulation.logic.reduction nor %x : !simulation.logic<8> -> !simulation.logic<1>
  %a = simulation.logic.is_true %narrow : !simulation.logic<4>
  %b = simulation.logic.is_true %xor : !simulation.logic<1>
  %c = simulation.logic.is_true %nor : !simulation.logic<1>
  return %a, %b, %c : i1, i1, i1
}

// A truncation is the low slice; its conversion converts only that slice.
// CHECK-LABEL: func.func @convert_resize
// CHECK-NOT: simulation.logic.resize
// CHECK: %[[LOW:.*]] = simulation.logic.extract %arg0 from 0 : !simulation.logic<4> -> !simulation.logic<1>
// CHECK: %[[BITS:.*]] = simulation.logic.to_bits %arg0 : !simulation.logic<4> -> i4
// CHECK: arith.extsi %[[BITS]] : i4 to i128
// CHECK: %[[UBITS:.*]] = simulation.logic.to_bits %arg0 : !simulation.logic<4> -> i4
// CHECK: arith.extui %[[UBITS]] : i4 to i65
// CHECK: simulation.logic.to_bits %[[LOW]] : !simulation.logic<1> -> i1
func.func @convert_resize(%x: !simulation.logic<4>) -> (i128, i65, i1) {
  %signed = simulation.logic.resize %x signed = true : !simulation.logic<4> -> !simulation.logic<128>
  %unsigned = simulation.logic.resize %x signed = false : !simulation.logic<4> -> !simulation.logic<65>
  %small = simulation.logic.resize %x signed = false : !simulation.logic<4> -> !simulation.logic<1>
  %a = simulation.logic.to_bits %signed : !simulation.logic<128> -> i128
  %b = simulation.logic.to_bits %unsigned : !simulation.logic<65> -> i65
  %c = simulation.logic.to_bits %small : !simulation.logic<1> -> i1
  return %a, %b, %c : i128, i65, i1
}

// CHECK-LABEL: func.func @keep_shared_result
// CHECK: %[[WIDE:.*]] = simulation.logic.resize %arg0 signed = true
// CHECK: simulation.logic.to_bits %arg0 : !simulation.logic<4> -> i4
// CHECK: arith.extsi
func.func @keep_shared_result(%x: !simulation.logic<4>) -> (!simulation.logic<65>, i65) {
  %wide = simulation.logic.resize %x signed = true : !simulation.logic<4> -> !simulation.logic<65>
  %bits = simulation.logic.to_bits %wide : !simulation.logic<65> -> i65
  return %wide, %bits : !simulation.logic<65>, i65
}

// CHECK-LABEL: func.func @dynamic_controls
// CHECK: simulation.logic.count_bits %arg0 matching %arg1, %arg2 :
func.func @dynamic_controls(%x: !simulation.logic<65>, %a: !simulation.logic<1>, %b: !simulation.logic<1>) -> i32 {
  %count = simulation.logic.count_bits %x matching %a, %b, %a, %b : (!simulation.logic<65>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) -> i32
  return %count : i32
}

func.func private @effect() -> !simulation.logic<1>
// CHECK-LABEL: func.func @one_bit_clog2
// CHECK: arith.constant 0 : i32
// CHECK: call @effect
// CHECK-NOT: simulation.logic.clog2
func.func @one_bit_clog2() -> i32 {
  %x = func.call @effect() : () -> !simulation.logic<1>
  %log = simulation.logic.clog2 %x : !simulation.logic<1>
  return %log : i32
}
