// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 11.5.1 and 19.6.1.2. The 65-bit value domains are
// symbolic. Variable bit addressing visits packed elements, not value tuples;
// constructor windows preserve X padding and self-determined index widths.
module top;
  bit [64:0] a;
  bit [0:64] ascending;
  bit [6:-58] negative;
  bit [1:0][3:0] packed_value;
  bit [6:0] position;
  bit b;
  covergroup cg(integer index);
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [0:64] cu: coverpoint ascending { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [6:-58] cn: coverpoint negative { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [1:0][3:0] cp: coverpoint packed_value { bins all = {[0:255]}; }
    bit [64:0] two: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    cb: coverpoint b { bins zero = {0}; }
    constant_bit: cross ca, cb {
      bins selected = constant_bit with (ca[0] == 0)
          matches 128'h1_0000_0000_0000_0000;
    }
    constructor_bit: cross ca, cb {
      bins selected = constructor_bit with (ca[index] == 0)
          matches 128'h1_0000_0000_0000_0000;
    }
    invalid_bit: cross ca, cb {
      bins selected = invalid_bit with (ca[index] === 1'bx) matches $;
    }
    fixed_slice: cross ca, cb {
      bins selected = fixed_slice with (ca[64:63] == 0)
          matches 64'h8000_0000_0000_0000;
    }
    constructor_slice: cross ca, cb {
      bins selected = constructor_slice with (ca[index +: 2] === 2'b00);
    }
    invalid_slice: cross ca, cb {
      bins selected = invalid_slice with (ca[index +: 2] === 2'bxx) matches $;
    }
    clipped_slice: cross ca, cb {
      bins selected = clipped_slice with (ca[65:64] === 2'bx0);
    }
    upward: cross cu, cb {
      bins selected = upward with (cu[index] == 0)
          matches 128'h1_0000_0000_0000_0000;
    }
    below_zero: cross cn, cb {
      bins selected = below_zero with (cn[-58] == 0)
          matches 128'h1_0000_0000_0000_0000;
    }
    packed_elements: cross cp, cb {
      bins selected = packed_elements with (cp[index] == 0) matches 16;
    }
    two_state: cross two, cb {
      bins selected = two_state with (two[65] === 1'b0) matches $;
    }
  endgroup
  covergroup variable_group;
    option.cross_retain_auto_bins = 0;
    bit [64:0] ca: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    bit [6:0] ci: coverpoint position { bins all = {[0:64]}; }
    product: cross ca, ci {
      bins selected = product with (ca[ci] == 1) matches (128'd65 << 64);
    }
  endgroup
  cg known, out_of_range, partial, unknown;
  variable_group variable_cov;
  int covered, total;
  real percentage;
  initial begin
    known = new(0);
    out_of_range = new(65);
    partial = new(-1);
    unknown = new(32'bx);
    variable_cov = new;
    known.sample();
    out_of_range.sample();
    partial.sample();
    unknown.sample();
    variable_cov.sample();
    percentage = known.constant_bit.get_inst_coverage(covered, total);
    $display("constant %.6f %0d %0d", percentage, covered, total);
    percentage = known.constructor_bit.get_inst_coverage(covered, total);
    $display("constructor %.6f %0d %0d", percentage, covered, total);
    percentage = out_of_range.invalid_bit.get_inst_coverage(covered, total);
    $display("outside %.6f %0d %0d", percentage, covered, total);
    percentage = partial.invalid_bit.get_inst_coverage(covered, total);
    $display("negative %.6f %0d %0d", percentage, covered, total);
    percentage = unknown.invalid_bit.get_inst_coverage(covered, total);
    $display("unknown %.6f %0d %0d", percentage, covered, total);
    percentage = known.fixed_slice.get_inst_coverage(covered, total);
    $display("slice %.6f %0d %0d", percentage, covered, total);
    percentage = known.constructor_slice.get_inst_coverage(covered, total);
    $display("window %.6f %0d %0d", percentage, covered, total);
    percentage = partial.constructor_slice.get_inst_coverage(covered, total);
    $display("partial %.6f %0d %0d", percentage, covered, total);
    percentage = unknown.invalid_slice.get_inst_coverage(covered, total);
    $display("unknown window %.6f %0d %0d", percentage, covered, total);
    percentage = known.clipped_slice.get_inst_coverage(covered, total);
    $display("clipped %.6f %0d %0d", percentage, covered, total);
    percentage = known.upward.get_inst_coverage(covered, total);
    $display("ascending %.6f %0d %0d", percentage, covered, total);
    percentage = known.below_zero.get_inst_coverage(covered, total);
    $display("negative range %.6f %0d %0d", percentage, covered, total);
    percentage = known.packed_elements.get_inst_coverage(covered, total);
    $display("packed %.6f %0d %0d", percentage, covered, total);
    percentage = known.two_state.get_inst_coverage(covered, total);
    $display("two-state %.6f %0d %0d", percentage, covered, total);
    percentage = variable_cov.product.get_inst_coverage(covered, total);
    $display("variable %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule
// $display values: with-clause selects use the original declared range and
// self-determined index (11.5.1); out-of-range, negative or X indexes give X
// in a four-state operand and 0 in a two-state one. A bin with "matches N"
// needs at least N satisfying value tuples and "matches $" needs all of them
// (19.6.1.2). "partial" has no satisfying tuple, so its bin is empty (0 0).
// CHECK: constant 100.000000 1 1
// CHECK-NEXT: constructor 100.000000 1 1
// CHECK-NEXT: outside 100.000000 1 1
// CHECK-NEXT: negative 100.000000 1 1
// CHECK-NEXT: unknown 100.000000 1 1
// CHECK-NEXT: slice 100.000000 1 1
// CHECK-NEXT: window 100.000000 1 1
// CHECK-NEXT: partial 0.000000 0 0
// CHECK-NEXT: unknown window 100.000000 1 1
// CHECK-NEXT: clipped 100.000000 1 1
// CHECK-NEXT: ascending 100.000000 1 1
// CHECK-NEXT: negative range 100.000000 1 1
// CHECK-NEXT: packed 100.000000 1 1
// CHECK-NEXT: two-state 100.000000 1 1
// CHECK-NEXT: variable 100.000000 1 1

// The report keeps the compiled select nodes: kind 22 is a bit select and 23
// an indexed part select. The ascending range keeps index_base=64 with the
// ascending flag, the negative range keeps base -58, the packed-element select
// has element_width=4, and no value tuple is materialized.
// JSON-DAG: "kind":22,"bit_width":1,"signedness":1,"ordinal":2,"flags":2,"element_width":1,"index_base":64
// JSON-DAG: "kind":22,"bit_width":1,"signedness":1,"ordinal":2,"flags":0,"element_width":1,"index_base":-58
// JSON-DAG: "kind":22,"bit_width":4,"signedness":1,"ordinal":2,"flags":0,"element_width":4,"index_base":0
// JSON-DAG: "kind":23,"bit_width":2,"signedness":1,"ordinal":2,"flags":0,"element_width":1,"index_base":0
// JSON: "resolved_functional_tuples":[]
