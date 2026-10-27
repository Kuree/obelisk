// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=cross-bin-iff | FileCheck %s --check-prefix=QUERY
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
module top;
  bit a;
  bit b;
  bit enabled;

  covergroup cg;
    a_point: coverpoint a {
      bins zero = {0};
      bins one = {1};
    }
    b_point: coverpoint b {
      bins zero = {0};
      bins one = {1};
    }
    product: cross a_point, b_point {
      bins selected = binsof(a_point.zero) iff (enabled);
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;

    // Clause 19.6: iff guards the explicit bin's counter. The selected tuple
    // remains owned by that bin and must not fall through to an automatic bin.
    a = 0;
    b = 0;
    enabled = 0;
    cov.sample();

    b = 1;
    enabled = 1;
    cov.sample();

    // A tuple outside the explicit selector still contributes to the retained
    // symbolic automatic cross.
    a = 1;
    b = 0;
    cov.sample();

    percentage = cov.product.get_inst_coverage(covered, total);
    $display("cross-bin-iff %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: cross-bin-iff 66.666667 2 3

// IEEE 1800-2023 19.6: the explicit bin keeps its one guarded hit, the
// retained automatic cross holds the two unselected products (one of them
// hit), and exactly one sparse tuple was recorded for it.
// JSON: "sparse_cross_tuples":[{"run":"{{[^"]*}}","instance":1,"cross":{{[0-9]+}},"hits":1,"overflow":false,"components":[{{[^]]*}}]}],"functional_source_ranges"
// JSON: "name":"product","comment":"","hierarchy":"top.cg.product"
// JSON-SAME: "automatic_total":2
// JSON-SAME: "bins":[{
// JSON-SAME: "name":"selected"
// JSON-SAME: "count":1,
// JSON-SAME: "automatic_bins":[{"name":"<one,zero>","count":1,{{[^]]*}}"components":[{{[^]]*}}],"contributors":[{{[^]]*}}]}]}
