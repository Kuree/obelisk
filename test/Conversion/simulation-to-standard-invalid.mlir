// RUN: not obelisk-opt --split-input-file \
// RUN:   --convert-obelisk-sim-values-to-standard %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not=unrealized_conversion_cast

// Keep this as a legalization-output check rather than verify-diagnostics:
// besides identifying each surviving operation, it asserts that partially
// converted unrealized casts never leak into any diagnostic dump.

// A logic element nested under a resource handle is deliberately not assigned
// a compiler-only representation.
module {
  // CHECK: failed to legalize operation 'func.func'
  func.func @ref_boundary(%arg: !simulation.ref<!simulation.logic<8>>) {
    return
  }
}

// -----

// Scheduler effects likewise require the future runtime conversion to join
// the same transaction.
module {
  func.func @scheduler_boundary() {
    // CHECK: failed to legalize operation 'simulation.ref.alloc'
    %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
    %ref = simulation.ref.alloc %value : !simulation.logic<8> -> !simulation.ref<!simulation.logic<8>>
    simulation.nba.enqueue %value to %ref : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
    return
  }
}

// -----

// Type-bearing descriptors and their design container are not restructured by
// the focused value pass.
module {
  simulation.design @descriptor_boundary {
    simulation.scope.decl 0
    // CHECK: failed to legalize operation 'simulation.storage.decl'
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
  }
}

// -----

// The focused pass never restructures simulation code units.
module {
  // CHECK: failed to legalize operation 'simulation.func'
  simulation.func @simulation_boundary(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %arg: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 8 : i32} {
    simulation.return
  }
}

// -----

// Type-bearing attributes participate in dialect-conversion legality instead
// of being diagnosed after a successful, already-committed conversion.
module {
  func.func @attribute_boundary() -> i1 {
    // CHECK: failed to legalize operation 'arith.constant'
    %value = "arith.constant"() <{value = 0 : i1}>
        {test.logic_type = !simulation.logic<1>} : () -> i1
    return %value : i1
  }
}
