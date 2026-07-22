// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard --canonicalize \
// RUN:   | FileCheck %s --implicit-check-not=!obelisk_sim.logic \
// RUN:       --implicit-check-not=unrealized_conversion_cast

// These checks inspect the returned value and unknown planes directly instead
// of using logic.case_eq from the lowering under test as the oracle.

// CHECK-LABEL: func.func @bitnot_z()
// CHECK-DAG: %[[BNV:.*]] = arith.constant false
// CHECK-DAG: %[[BNU:.*]] = arith.constant true
// CHECK: return %[[BNV]], %[[BNU]] : i1, i1
func.func @bitnot_z() -> !obelisk_sim.logic<1> {
  %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 : !obelisk_sim.logic<1>
  %result = obelisk_sim.logic.unary bit_not %z
      : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
  return %result : !obelisk_sim.logic<1>
}

// CHECK-LABEL: func.func @x_and_zero()
// CHECK: %[[AZ:.*]] = arith.constant false
// CHECK: return %[[AZ]], %[[AZ]] : i1, i1
func.func @x_and_zero() -> !obelisk_sim.logic<1> {
  %x = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
  %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
  %result = obelisk_sim.logic.binary and %x, %zero
      : !obelisk_sim.logic<1>
  return %result : !obelisk_sim.logic<1>
}

// CHECK-LABEL: func.func @division_zero()
// CHECK-DAG: %[[DZV:.*]] = arith.constant 0 : i5
// CHECK-DAG: %[[DZU:.*]] = arith.constant -1 : i5
// CHECK: return %[[DZV]], %[[DZU]] : i5, i5
func.func @division_zero() -> !obelisk_sim.logic<5> {
  %ten = obelisk_sim.logic.constant 10 : i5, 0 : i5 : !obelisk_sim.logic<5>
  %zero = obelisk_sim.logic.constant 0 : i5, 0 : i5 : !obelisk_sim.logic<5>
  %result = obelisk_sim.logic.binary udiv %ten, %zero
      : !obelisk_sim.logic<5>
  return %result : !obelisk_sim.logic<5>
}

// CHECK-LABEL: func.func @partial_dynamic()
// CHECK-DAG: %[[PDV:.*]] = arith.constant 2 : i3
// CHECK-DAG: %[[PDU:.*]] = arith.constant 1 : i3
// CHECK: return %[[PDV]], %[[PDU]] : i3, i3
func.func @partial_dynamic() -> !obelisk_sim.logic<3> {
  %input = obelisk_sim.logic.constant 17 : i5, 4 : i5 : !obelisk_sim.logic<5>
  %low = obelisk_sim.logic.constant -1 : i5, 0 : i5 : !obelisk_sim.logic<5>
  %result = obelisk_sim.logic.dyn_extract %input from %low
      : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>)
          -> !obelisk_sim.logic<3>
  return %result : !obelisk_sim.logic<3>
}

// CHECK-LABEL: func.func @replicate_planes()
// CHECK-DAG: %[[RPV:.*]] = arith.constant -22 : i6
// CHECK-DAG: %[[RPU:.*]] = arith.constant 21 : i6
// CHECK: return %[[RPV]], %[[RPU]] : i6, i6
func.func @replicate_planes() -> !obelisk_sim.logic<6> {
  %input = obelisk_sim.logic.constant 2 : i2, 1 : i2 : !obelisk_sim.logic<2>
  %result = obelisk_sim.logic.replicate %input times 3
      : !obelisk_sim.logic<2> -> !obelisk_sim.logic<6>
  return %result : !obelisk_sim.logic<6>
}

// A non-power-of-two count exercises placement of multiple doubled chunks.
// CHECK-LABEL: func.func @replicate_five()
// CHECK-DAG: %[[RFV:.*]] = arith.constant -342 : i10
// CHECK-DAG: %[[RFU:.*]] = arith.constant 341 : i10
// CHECK: return %[[RFV]], %[[RFU]] : i10, i10
func.func @replicate_five() -> !obelisk_sim.logic<10> {
  %input = obelisk_sim.logic.constant 2 : i2, 1 : i2 : !obelisk_sim.logic<2>
  %result = obelisk_sim.logic.replicate %input times 5
      : !obelisk_sim.logic<2> -> !obelisk_sim.logic<10>
  return %result : !obelisk_sim.logic<10>
}
