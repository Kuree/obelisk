// RUN: obelisk-opt %s --canonicalize | FileCheck %s

// IEEE 1800-2023 9.4.1 and 3.14.1: a constant delay such as `#(p / 2)` or
// `#(p / 2.0)` is scaled and rounded once to the time precision. Folding
// follows the native lowering and the bytecode intrinsic: NaN and negative
// real delays are zero, ties round up, and an unrepresentable delay
// saturates.

// CHECK-LABEL: func.func @integral
// CHECK-NEXT: %[[T:.*]] = simulation.time.constant 5000
// CHECK-NEXT: return %[[T]]
func.func @integral() -> !simulation.time {
  %five = arith.constant 5 : i64
  %ticks = simulation.time.scale %five by 1000 signed = false : i64
  return %ticks : !simulation.time
}

// A product past the time range stays dynamic.
// CHECK-LABEL: func.func @integral_overflow
// CHECK: simulation.time.scale
func.func @integral_overflow() -> !simulation.time {
  %large = arith.constant 4611686018427387904 : i64
  %ticks = simulation.time.scale %large by 4 signed = false : i64
  return %ticks : !simulation.time
}

// CHECK-LABEL: func.func @half_cycle
// CHECK-NEXT: %[[T:.*]] = simulation.time.constant 500
// CHECK-NEXT: return %[[T]]
func.func @half_cycle() -> !simulation.time {
  %cycle = arith.constant 1000 : i32
  %real = simulation.real.from_integer %cycle signed = true : i32 -> f64
  %two = arith.constant 2.0 : f64
  %half = arith.divf %real, %two : f64
  %ticks = simulation.time.from_real %half by 1 quantum 1
  return %ticks : !simulation.time
}

// CHECK-LABEL: func.func @rounding
// CHECK-DAG: %[[ZERO:.*]] = simulation.time.constant 0
// CHECK-DAG: %[[TIE:.*]] = simulation.time.constant 3
// CHECK-DAG: %[[QUANTUM:.*]] = simulation.time.constant 1230
// CHECK: return %[[ZERO]], %[[ZERO]], %[[TIE]], %[[QUANTUM]]
func.func @rounding() -> (!simulation.time, !simulation.time, !simulation.time,
                          !simulation.time) {
  %nan = arith.constant 0x7FF8000000000000 : f64
  %negative = arith.constant -4.0 : f64
  %tie = arith.constant 2.5 : f64
  %fraction = arith.constant 1.2345 : f64
  %a = simulation.time.from_real %nan by 1 quantum 1
  %b = simulation.time.from_real %negative by 1 quantum 1
  %c = simulation.time.from_real %tie by 1 quantum 1
  // 1.2345 time units at a 1000-tick unit and 10-tick precision.
  %d = simulation.time.from_real %fraction by 1000 quantum 10
  return %a, %b, %c, %d : !simulation.time, !simulation.time, !simulation.time,
                          !simulation.time
}

// A saturated delay exceeds the i64 time-constant range and stays dynamic.
// CHECK-LABEL: func.func @saturates
// CHECK: simulation.time.from_real
func.func @saturates() -> !simulation.time {
  %infinity = arith.constant 0x7FF0000000000000 : f64
  %ticks = simulation.time.from_real %infinity by 1 quantum 1
  return %ticks : !simulation.time
}

// CHECK-LABEL: func.func @integers
// CHECK-DAG: arith.constant -1.000000e+00 : f64
// CHECK-DAG: arith.constant 2.550000e+02 : f64
// CHECK-DAG: arith.constant 0x4340000000000000 : f64
// CHECK-DAG: arith.constant 1.2676506002282294E+30 : f64
// CHECK-NOT: simulation.real.from_integer
func.func @integers() -> (f64, f64, f64, f64) {
  %byte = arith.constant -1 : i8
  %signed = simulation.real.from_integer %byte signed = true : i8 -> f64
  %unsigned = simulation.real.from_integer %byte signed = false : i8 -> f64
  // 2^53 + 1 rounds to even.
  %odd = arith.constant 9007199254740993 : i64
  %even = simulation.real.from_integer %odd signed = false : i64 -> f64
  // 2^100 is exact.
  %wide = arith.constant 1267650600228229401496703205376 : i128
  %large = simulation.real.from_integer %wide signed = false : i128 -> f64
  return %signed, %unsigned, %even, %large : f64, f64, f64, f64
}
