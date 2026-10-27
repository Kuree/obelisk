// RUN: talos-opt %s --canonicalize | FileCheck %s

// Reversing one lane or the same lanes twice keeps every lane in place, and a
// lane resize to the same width is the identity. An integer, a conversion from
// integer bits and a logic constant without X/Z have a known unknown plane.

// CHECK-LABEL: func.func @reverse
// CHECK-NEXT: %[[KEEP:.*]] = schedule.word.reverse %arg0 {lane_width = 8 : i32}
// CHECK-NEXT: %[[ONCE:.*]] = schedule.word.reverse %arg0 {lane_width = 8 : i32}
// CHECK-NEXT: %[[OTHER:.*]] = schedule.word.reverse %[[ONCE]] {lane_width = 4 : i32}
// CHECK-NEXT: return %arg0, %arg0, %[[KEEP]], %[[OTHER]]
func.func @reverse(%x: !simulation.logic<16>) -> (!simulation.logic<16>, !simulation.logic<16>, !simulation.logic<16>, !simulation.logic<16>) {
  %whole = schedule.word.reverse %x {lane_width = 16 : i32} : (!simulation.logic<16>) -> !simulation.logic<16>
  %once = schedule.word.reverse %x {lane_width = 8 : i32} : (!simulation.logic<16>) -> !simulation.logic<16>
  %twice = schedule.word.reverse %once {lane_width = 8 : i32} : (!simulation.logic<16>) -> !simulation.logic<16>
  %keep = schedule.word.reverse %x {lane_width = 8 : i32} : (!simulation.logic<16>) -> !simulation.logic<16>
  %once4 = schedule.word.reverse %x {lane_width = 8 : i32} : (!simulation.logic<16>) -> !simulation.logic<16>
  %other = schedule.word.reverse %once4 {lane_width = 4 : i32} : (!simulation.logic<16>) -> !simulation.logic<16>
  return %whole, %twice, %keep, %other : !simulation.logic<16>, !simulation.logic<16>, !simulation.logic<16>, !simulation.logic<16>
}

// CHECK-LABEL: func.func @resize
// CHECK-NEXT: %[[WIDE:.*]] = schedule.word.resize_lanes %arg0
// CHECK-NEXT: return %arg0, %[[WIDE]]
func.func @resize(%x: i16) -> (i16, i32) {
  %same = schedule.word.resize_lanes %x {input_lane_width = 8 : i32, result_lane_width = 8 : i32} : (i16) -> i16
  %wide = schedule.word.resize_lanes %x {input_lane_width = 8 : i32, result_lane_width = 16 : i32} : (i16) -> i32
  return %same, %wide : i16, i32
}

// CHECK-LABEL: func.func @known
// CHECK-DAG: %[[TRUE:.*]] = arith.constant true
// CHECK-DAG: %[[FALSE:.*]] = arith.constant false
// CHECK: %[[OPEN:.*]] = schedule.known.value %arg1
// CHECK: return %[[TRUE]], %[[TRUE]], %[[TRUE]], %[[FALSE]], %[[OPEN]]
func.func @known(%bits: i8, %logic: !simulation.logic<8>) -> (i1, i1, i1, i1, i1) {
  %from = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
  %k0 = schedule.known.value %bits : (i8) -> i1
  %k1 = schedule.known.value %from : (!simulation.logic<8>) -> i1
  %clean = simulation.logic.constant 5 : i8, 0 : i8 : !simulation.logic<8>
  %k2 = schedule.known.value %clean : (!simulation.logic<8>) -> i1
  %dirty = simulation.logic.constant 5 : i8, 2 : i8 : !simulation.logic<8>
  %k3 = schedule.known.value %dirty : (!simulation.logic<8>) -> i1
  %k4 = schedule.known.value %logic : (!simulation.logic<8>) -> i1
  return %k0, %k1, %k2, %k3, %k4 : i1, i1, i1, i1, i1
}
