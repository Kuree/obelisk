// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

// CHECK: module attributes {
// CHECK-SAME: runtime.execution_flags = #runtime.execution_flags<has_bytecode|require_bytecode|dpi_exports>
// CHECK-SAME: runtime.features = #runtime.features<dynamic_scan|class_bitstream>
// CHECK-SAME: runtime.sampled_ranges = array<i64: 0, 8>
// CHECK-SAME: runtime.state_bits = 8 : i64
module attributes {
  runtime.execution_flags =
      #runtime.execution_flags<dpi_exports|has_bytecode|require_bytecode>,
  runtime.features = #runtime.features<class_bitstream|dynamic_scan>,
  runtime.state_bits = 8 : i64, runtime.sampled_ranges = array<i64: 0, 8>} {
  // CHECK-LABEL: func.func private @encoded
  // CHECK-SAME: runtime.bytecode_function = 3 : i32
  func.func private @encoded() attributes {runtime.bytecode_function = 3 : i32}
}
