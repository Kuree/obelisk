// RUN: obelisk-opt --convert-obelisk-sim-to-runtime %s | FileCheck %s

// The token data is a four-state value, so it lowers to a value plane and an
// unknown plane while the kind and the address stay single values. Consume all
// three results to pin that one-to-N result mapping.

module {
  func.func @readmem_token_results(%ctx: !simulation.context, %fd_bits: i32)
      -> (!simulation.logic<24>, i32, i64)
      attributes {simulation.hierarchical_name = "top.readmem_token_results"} {
    %data, %kind, %address =
        simulation.file.readmem_token %ctx, %fd_bits {radix = #simulation.radix<binary>} :
        (!simulation.context, i32) -> (!simulation.logic<24>, i32, i64)
    return %data, %kind, %address : !simulation.logic<24>, i32, i64
  }
}

// CHECK-LABEL: func.func @readmem_token_results
// CHECK-SAME: -> (i24, i24, i32, i64)
// CHECK: %[[VALUE_SCRATCH:.*]] = runtime.bytes.scratch 3
// CHECK: %[[UNKNOWN_SCRATCH:.*]] = runtime.bytes.scratch 3
// CHECK: %[[STATUS:.*]], %[[KIND:.*]], %[[ADDRESS:.*]] = runtime.file.readmem_token
// CHECK: %[[VALUE:.*]] = runtime.bytes.to_packed %[[VALUE_SCRATCH]]{{.*}}least_significant_byte_first = true
// CHECK: %[[UNKNOWN:.*]] = runtime.bytes.to_packed %[[UNKNOWN_SCRATCH]]{{.*}}least_significant_byte_first = true
// CHECK: return %[[VALUE]], %[[UNKNOWN]], %[[KIND]], %[[ADDRESS]] : i24, i24, i32, i64
