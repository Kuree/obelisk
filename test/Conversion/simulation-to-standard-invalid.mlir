// RUN: not obelisk-opt --split-input-file \
// RUN:   --convert-obelisk-sim-values-to-standard %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not=unrealized_conversion_cast

// A logic element nested under a resource handle is deliberately not assigned
// a compiler-only representation.
module {
  // CHECK: failed to legalize operation 'func.func'
  func.func @ref_boundary(%arg: !obelisk_sim.ref<!obelisk_sim.logic<8>>) {
    return
  }
}

// -----

// Scheduler effects likewise require the future runtime conversion to join
// the same transaction.
module {
  func.func @scheduler_boundary() {
    // CHECK: failed to legalize operation 'obelisk_sim.ref.alloc'
    %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
    %ref = obelisk_sim.ref.alloc %value : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
    obelisk_sim.nba.enqueue %value to %ref : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
    return
  }
}

// -----

// Type-bearing descriptors and their design container are not restructured by
// the focused value pass.
module {
  obelisk_sim.design @descriptor_boundary {
    obelisk_sim.scope.decl 0
    // CHECK: failed to legalize operation 'obelisk_sim.storage.decl'
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
  }
}

// -----

// The focused pass never restructures obelisk_sim code units.
module {
  // CHECK: failed to legalize operation 'obelisk_sim.func'
  obelisk_sim.func @simulation_boundary(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %arg: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 8 : i32} {
    obelisk_sim.return
  }
}

// -----

// Type-bearing attributes participate in dialect-conversion legality instead
// of being diagnosed after a successful, already-committed conversion.
module {
  func.func @attribute_boundary() -> i1 {
    // CHECK: failed to legalize operation 'arith.constant'
    %value = "arith.constant"() <{value = 0 : i1}>
        {test.logic_type = !obelisk_sim.logic<1>} : () -> i1
    return %value : i1
  }
}
