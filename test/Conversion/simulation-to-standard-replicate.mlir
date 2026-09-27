// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | FileCheck %s --check-prefix=SHIFT \
// RUN:       --implicit-check-not=!simulation.logic \
// RUN:       --implicit-check-not=unrealized_conversion_cast
// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | FileCheck %s --check-prefix=OR

// Replication uses binary doubling. A count of 1024 therefore needs ten
// doubling shifts per plane, rather than 1024 shifts per plane.

// SHIFT-LABEL: func.func @replicate_1024(
// SHIFT-COUNT-20: arith.shli
// SHIFT: return
// OR-LABEL: func.func @replicate_1024(
// OR-COUNT-22: arith.ori
// OR: return
func.func @replicate_1024(%input: !simulation.logic<1>)
    -> !simulation.logic<1024> {
  %result = simulation.logic.replicate %input times 1024
      : !simulation.logic<1> -> !simulation.logic<1024>
  return %result : !simulation.logic<1024>
}
