// RUN: talos-opt %s | talos-opt | FileCheck %s

// CHECK: module attributes {
// CHECK-SAME: coverage.language_version = 2023 : i32
// CHECK-SAME: coverage.metrics = ["line", "toggle"]
// CHECK-SAME: coverage.toggle_bit_count = 6 : i64
module attributes {coverage.metrics = ["line", "toggle"],
                   coverage.language_version = 2023 : i32,
                   coverage.toggle_bit_count = 6 : i64} {
  // CHECK-LABEL: func.func private @declaration
  // CHECK-SAME: coverage.source_authored
  // CHECK-SAME: coverage.toggle_bindings = [#coverage.toggle_binding<base = 0, low = 0, width = 4>, #coverage.toggle_binding<base = 4, low = 2, width = 2>]
  // CHECK-SAME: coverage.toggle_observable
  func.func private @declaration() attributes {
    coverage.source_authored, coverage.toggle_observable,
    coverage.toggle_bindings = [
      #coverage.toggle_binding<base = 0, low = 0, width = 4>,
      #coverage.toggle_binding<base = 4, low = 2, width = 2>]}
}
