// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"role":11'

// IEEE 1800-2023 19.6.1.2 counts distinct associated values in each bin tuple.
// A parent with evaluates all values in the child's selected bins, including
// values that did not satisfy the child's predicate.
module top;
  bit [64:0] value;
  bit partner;
  logic signed [2:0] state;

  covergroup cg(int need);
    option.cross_retain_auto_bins = 0;
    point: coverpoint value {
      bins low = {[65'h0_ffff_ffff_ffff_fffe:65'h1_0000_0000_0000_0000]};
    }
    side: coverpoint partner { bins zero = {0}; }
    product: cross point, side {
      bins selected = binsof(point.low) with
          (point < 65'h1_0000_0000_0000_0000) matches need;
    }
    nested: cross point, side {
      bins selected = (nested with (point == 65'h0_ffff_ffff_ffff_fffe))
          with (point == 65'h1_0000_0000_0000_0000);
    }
    universal: cross point, side {
      bins selected = universal with (point >= 65'h0_ffff_ffff_ffff_fffe) matches $;
    }
    rejected: cross point, side {
      bins selected = rejected with (point < 65'h1_0000_0000_0000_0000) matches $;
    }
    states: coverpoint state {
      // 19.5.7 excludes these non-wildcard singleton values from the bins.
      bins ignored_x = {3'bx01};
      bins ignored_z = {3'bz10};
      bins negative = {-1};
    }
    four_state: cross states, side {
      bins selected = four_state with (states === 3'bx01 || states === -1);
    }
  endgroup

  cg cov;
  int covered, total;
  real percentage;
  initial begin
    cov = new(2);
    value = 65'h1_0000_0000_0000_0000;
    partner = 0;
    state = 3'bx01;
    cov.sample();
    state = -1;
    cov.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("count %.6f %0d %0d", percentage, covered, total);
    percentage = cov.nested.get_inst_coverage(covered, total);
    $display("nested %.6f %0d %0d", percentage, covered, total);
    percentage = cov.universal.get_inst_coverage(covered, total);
    $display("all %.6f %0d %0d", percentage, covered, total);
    percentage = cov.rejected.get_inst_coverage(covered, total);
    $display("rejected %.6f %0d %0d", percentage, covered, total);
    percentage = cov.four_state.get_inst_coverage(covered, total);
    $display("four state %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: count 100.000000 1 1
// CHECK-NEXT: nested 100.000000 1 1
// CHECK-NEXT: all 100.000000 1 1
// CHECK-NEXT: rejected 0.000000 0 0
// CHECK-NEXT: four state 100.000000 1 1

// Each with clause is stored as a selector predicate root, not as dense
// FunctionalExpression batches (no role 11 expressions) or resolved tuple sets;
// every resolved selector binding carries the same flags.
// JSON: "cross_selector_nodes":[
// JSON-COUNT-6: "predicate_root":{{[1-9][0-9]*}}
// JSON: "resolved_cross_selector_bindings":[
// JSON-COUNT-6: "flags":4}
// JSON-NOT: "flags":
// JSON: "resolved_functional_tuple_sets":[]
