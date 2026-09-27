// RUN: not obelisk-opt %s 2>&1 | FileCheck %s

!bytes = !simulation.unpacked_array<0 : 1 x i8>

module {
  func.func @corrupt_plan(%source: !bytes) -> i16 {
    // The repeat stride is seven instead of the canonical eight bits.
    %result = simulation.aggregate.export_bitstream %source plan
        [5407724624, 2, 16, 16,
         4294967298, 0, 2, 7, 8, 8,
         1, 0, 8, 0, 0, 8] : (!bytes) -> i16
    return %result : i16
  }

  func.func @corrupt_import(%source: i16) -> !bytes {
    // The same compact plan is validated against the target layout.
    %result = simulation.aggregate.import_bitstream %source plan
        [5407724624, 2, 16, 16,
         4294967298, 0, 2, 7, 8, 8,
         1, 0, 8, 0, 0, 8] : (i16) -> !bytes
    return %result : !bytes
  }
}

// CHECK: error: 'simulation.aggregate.export_bitstream' op plan does not match the fixed aggregate layout
// CHECK: error: 'simulation.aggregate.import_bitstream' op plan does not match the fixed aggregate layout
