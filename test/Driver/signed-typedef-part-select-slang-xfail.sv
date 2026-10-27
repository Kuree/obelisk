// XFAIL: *
// RUN: %talos -emit-slang %s -o - | FileCheck %s

// IEEE 1800-2023 11.8.1: a part-select is unsigned even when it selects a
// whole signed vector, and 6.18 makes a typedef name for the same type. Slang
// v11 tests for a packed array before resolving the type alias, so a part-select
// of a signed typedef keeps the signedness and later widens by sign extension.
module top;
  typedef logic signed [1:0] T;
  T value;
  logic [2:0] result;
  initial result = value[1:0];
endmodule

// CHECK: slang.expression.range_select attributes {is_signed = false
