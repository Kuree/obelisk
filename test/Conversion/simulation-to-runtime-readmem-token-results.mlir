// RUN: obelisk-opt --convert-obelisk-sim-to-runtime %s | FileCheck %s

// The token data is a four-state value, so it lowers to a value plane and an
// unknown plane while the kind and the address stay single values. Consume all
// three results to pin that one-to-N result mapping.

module {
  func.func @readmem_token_results(%ctx: !obelisk_sim.context, %fd_bits: i32)
      -> (!obelisk_sim.logic<8>, i32, i64)
      attributes {obelisk_sim.hierarchical_name = "top.readmem_token_results"} {
    %data, %kind, %address =
        obelisk_sim.file.readmem_token %ctx, %fd_bits {radix = 2 : i32} :
        (!obelisk_sim.context, i32) -> (!obelisk_sim.logic<8>, i32, i64)
    return %data, %kind, %address : !obelisk_sim.logic<8>, i32, i64
  }
}

// CHECK-LABEL: func.func @readmem_token_results
// CHECK-SAME: -> (i8, i8, i32, i64)
// CHECK: %[[VALUE_SCRATCH:.*]] = obelisk_rt.bytes.scratch 1
// CHECK: %[[UNKNOWN_SCRATCH:.*]] = obelisk_rt.bytes.scratch 1
// CHECK: %[[STATUS:.*]], %[[KIND:.*]], %[[ADDRESS:.*]] = obelisk_rt.file.readmem_token
// CHECK: %[[VALUE:.*]] = obelisk_rt.bytes.to_packed %[[VALUE_SCRATCH]]
// CHECK: %[[UNKNOWN:.*]] = obelisk_rt.bytes.to_packed %[[UNKNOWN_SCRATCH]]
// CHECK: return %[[VALUE]], %[[UNKNOWN]], %[[KIND]], %[[ADDRESS]] : i8, i8, i32, i64
