// RUN: cd %S/../Inputs/sdf && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2017 32.4/.5 and 3.14.1 require exact decimal rounding even when
// the first material digit is followed by hundreds of fractional digits.
// CHECK-DAG: hierarchical_name = "sdf_fractional_boundaries.below"{{.*}}timing_delay_fs = array<i64: 0>
// CHECK-DAG: hierarchical_name = "sdf_fractional_boundaries.exact"{{.*}}timing_delay_fs = array<i64: 10>
// CHECK-DAG: hierarchical_name = "sdf_fractional_boundaries.above"{{.*}}timing_delay_fs = array<i64: 10>

module sdf_fractional_leaf(input wire source,
                           output wire destination);
  timeunit 1ns;
  timeprecision 10fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_fractional_boundaries;
  timeunit 1ns;
  timeprecision 10fs;
  logic source;
  wire below_destination, exact_destination, above_destination;
  sdf_fractional_leaf below(source, below_destination);
  sdf_fractional_leaf exact(source, exact_destination);
  sdf_fractional_leaf above(source, above_destination);
  initial $sdf_annotate("fractional-boundaries.sdf");
endmodule
