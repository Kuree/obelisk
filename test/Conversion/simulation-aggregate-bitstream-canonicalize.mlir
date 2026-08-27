// RUN: obelisk-opt %s --canonicalize | FileCheck %s

!bytes = !obelisk_sim.unpacked_array<0 : 1 x i8>
!nibbles = !obelisk_sim.unpacked_array<0 : 1 x !obelisk_sim.logic<4>>

module {
  func.func @fold_known_bits() -> i16 {
    %hi = arith.constant 18 : i8
    %lo = arith.constant 52 : i8
    %source = obelisk_sim.aggregate.construct %hi, %lo :
        (i8, i8) -> !bytes
    %result = obelisk_sim.aggregate.export_bitstream %source plan
        [5407724624, 2, 16, 16,
         4294967298, 0, 2, 8, 8, 8,
         1, 0, 8, 0, 0, 8] : (!bytes) -> i16
    return %result : i16
  }

  func.func @fold_unknown_bits_to_zero() -> i8 {
    %hi = obelisk_sim.logic.constant 15 : i4, 3 : i4 :
        !obelisk_sim.logic<4>
    %lo = obelisk_sim.logic.constant 12 : i4, 0 : i4 :
        !obelisk_sim.logic<4>
    %source = obelisk_sim.aggregate.construct %hi, %lo :
        (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !nibbles
    %result = obelisk_sim.aggregate.export_bitstream %source plan
        [5407724624, 2, 8, 8,
         4294967298, 0, 2, 4, 4, 4,
         1, 0, 4, 0, 0, 4] : (!nibbles) -> i8
    return %result : i8
  }
}

// CHECK-LABEL: func.func @fold_known_bits
// CHECK: %[[KNOWN:.*]] = arith.constant 4660 : i16
// CHECK: return %[[KNOWN]]
// CHECK-LABEL: func.func @fold_unknown_bits_to_zero
// CHECK-NOT: obelisk_sim.aggregate.export_bitstream
// CHECK: %[[COERCED:.*]] = arith.constant -52 : i8
// CHECK: return %[[COERCED]]
