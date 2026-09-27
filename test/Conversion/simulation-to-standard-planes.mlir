// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard --canonicalize \
// RUN:   | FileCheck %s --implicit-check-not=!simulation.logic \
// RUN:       --implicit-check-not=unrealized_conversion_cast

// These checks inspect the returned value and unknown planes directly instead
// of using logic.case_eq from the lowering under test as the oracle.

// CHECK-LABEL: func.func @bitnot_z()
// CHECK-DAG: %[[BNV:.*]] = arith.constant false
// CHECK-DAG: %[[BNU:.*]] = arith.constant true
// CHECK: return %[[BNV]], %[[BNU]] : i1, i1
func.func @bitnot_z() -> !simulation.logic<1> {
  %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %result = simulation.logic.unary bit_not %z
      : (!simulation.logic<1>) -> !simulation.logic<1>
  return %result : !simulation.logic<1>
}

// CHECK-LABEL: func.func @x_and_zero()
// CHECK: %[[AZ:.*]] = arith.constant false
// CHECK: return %[[AZ]], %[[AZ]] : i1, i1
func.func @x_and_zero() -> !simulation.logic<1> {
  %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
  %result = simulation.logic.binary and %x, %zero
      : !simulation.logic<1>
  return %result : !simulation.logic<1>
}

// CHECK-LABEL: func.func @division_zero()
// CHECK-DAG: %[[DZV:.*]] = arith.constant 0 : i5
// CHECK-DAG: %[[DZU:.*]] = arith.constant -1 : i5
// CHECK: return %[[DZV]], %[[DZU]] : i5, i5
func.func @division_zero() -> !simulation.logic<5> {
  %ten = simulation.logic.constant 10 : i5, 0 : i5 : !simulation.logic<5>
  %zero = simulation.logic.constant 0 : i5, 0 : i5 : !simulation.logic<5>
  %result = simulation.logic.binary udiv %ten, %zero
      : !simulation.logic<5>
  return %result : !simulation.logic<5>
}

// CHECK-LABEL: func.func @partial_dynamic()
// CHECK-DAG: %[[PDV:.*]] = arith.constant 2 : i3
// CHECK-DAG: %[[PDU:.*]] = arith.constant 1 : i3
// CHECK: return %[[PDV]], %[[PDU]] : i3, i3
func.func @partial_dynamic() -> !simulation.logic<3> {
  %input = simulation.logic.constant 17 : i5, 4 : i5 : !simulation.logic<5>
  %low = simulation.logic.constant -1 : i5, 0 : i5 : !simulation.logic<5>
  %result = simulation.logic.dyn_extract %input from %low
      : (!simulation.logic<5>, !simulation.logic<5>)
          -> !simulation.logic<3>
  return %result : !simulation.logic<3>
}

// CHECK-LABEL: func.func @replicate_planes()
// CHECK-DAG: %[[RPV:.*]] = arith.constant -22 : i6
// CHECK-DAG: %[[RPU:.*]] = arith.constant 21 : i6
// CHECK: return %[[RPV]], %[[RPU]] : i6, i6
func.func @replicate_planes() -> !simulation.logic<6> {
  %input = simulation.logic.constant 2 : i2, 1 : i2 : !simulation.logic<2>
  %result = simulation.logic.replicate %input times 3
      : !simulation.logic<2> -> !simulation.logic<6>
  return %result : !simulation.logic<6>
}

// A non-power-of-two count exercises placement of multiple doubled chunks.
// CHECK-LABEL: func.func @replicate_five()
// CHECK-DAG: %[[RFV:.*]] = arith.constant -342 : i10
// CHECK-DAG: %[[RFU:.*]] = arith.constant 341 : i10
// CHECK: return %[[RFV]], %[[RFU]] : i10, i10
func.func @replicate_five() -> !simulation.logic<10> {
  %input = simulation.logic.constant 2 : i2, 1 : i2 : !simulation.logic<2>
  %result = simulation.logic.replicate %input times 5
      : !simulation.logic<2> -> !simulation.logic<10>
  return %result : !simulation.logic<10>
}
