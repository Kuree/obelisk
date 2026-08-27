// RUN: not obelisk-opt %s 2>&1 | FileCheck %s

!bytes = !obelisk_sim.unpacked_array<0 : 1 x i8>

module {
  func.func @corrupt_plan(%source: !bytes) -> i16 {
    // The repeat stride is seven instead of the canonical eight bits.
    %result = obelisk_sim.aggregate.export_bitstream %source plan
        [5407724624, 2, 16, 16,
         4294967298, 0, 2, 7, 8, 8,
         1, 0, 8, 0, 0, 8] : (!bytes) -> i16
    return %result : i16
  }
}

// CHECK: error: 'obelisk_sim.aggregate.export_bitstream' op plan does not match the fixed aggregate layout
