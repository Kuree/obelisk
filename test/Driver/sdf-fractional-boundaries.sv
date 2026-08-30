// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py fractional-boundaries %t
// RUN: obelisk -emit-slang %t.sv -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2017 32.4/.5 and 3.14.1 require exact decimal rounding even when
// the first material digit is followed by thousands of fractional digits.
// CHECK-DAG: hierarchical_name = "sdf_fractional_boundaries.below"{{.*}}timing_delay_fs = array<i64: 0>
// CHECK-DAG: hierarchical_name = "sdf_fractional_boundaries.exact"{{.*}}timing_delay_fs = array<i64: 10>
// CHECK-DAG: hierarchical_name = "sdf_fractional_boundaries.above"{{.*}}timing_delay_fs = array<i64: 10>
