// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | FileCheck %s --implicit-check-not=!simulation.logic \
// RUN:       --implicit-check-not=unrealized_conversion_cast

// A full-width dynamic selection still uses a fixed number of operations: one
// guarded shift for each four-state plane, not one expansion per result bit.

// CHECK-LABEL: func.func @logic_wide(
// CHECK-COUNT-2: arith.shrui
// CHECK: return
func.func @logic_wide(%input: !simulation.logic<65>, %low: i65)
    -> !simulation.logic<65> {
  %result = simulation.logic.dyn_extract %input from %low
      : (!simulation.logic<65>, i65) -> !simulation.logic<65>
  return %result : !simulation.logic<65>
}

// CHECK-LABEL: func.func @bits_wide(
// CHECK-COUNT-1: arith.shrui
// CHECK: return
func.func @bits_wide(%input: i65, %low: !simulation.logic<65>) -> i65 {
  %result = simulation.bits.dyn_extract %input from %low
      : (i65, !simulation.logic<65>) -> i65
  return %result : i65
}
