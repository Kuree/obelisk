// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=explicit-cross > %t.native.out
// RUN: %t.native --coverage-output=%t.native.reverse.obcov \
// RUN:   --coverage-test=explicit-cross-reverse +reverse > %t.native.reverse.out
// RUN: obelisk-cov merge -o %t.native.merged.obcov \
// RUN:   %t.native.obcov %t.native.reverse.obcov
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=explicit-cross > %t.bytecode.out
// RUN: %t.bytecode --coverage-output=%t.bytecode.reverse.obcov \
// RUN:   --coverage-test=explicit-cross-reverse +reverse > %t.bytecode.reverse.out
// RUN: obelisk-cov merge -o %t.bytecode.merged.obcov \
// RUN:   %t.bytecode.obcov %t.bytecode.reverse.obcov
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.reverse.out %t.bytecode.reverse.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.report
// RUN: FileCheck %s --check-prefix=REPORT < %t.report
// RUN: %t.native --coverage-load=%t.native.obcov \
// RUN:   --coverage-output=%t.native.loaded.obcov \
// RUN:   --coverage-test=explicit-cross-loaded > %t.native.loaded.out
// RUN: %t.bytecode --coverage-load=%t.bytecode.obcov \
// RUN:   --coverage-output=%t.bytecode.loaded.obcov \
// RUN:   --coverage-test=explicit-cross-loaded > %t.bytecode.loaded.out
// RUN: diff -u %t.native.loaded.out %t.bytecode.loaded.out
// RUN: FileCheck %s --check-prefix=LOADED < %t.native.loaded.out

module top;
  bit [1:0] a;
  bit b;
  bit c;
  bit empty_value;

  covergroup cg(input int selected_value);
    type_option.merge_instances = 1;
    a_point: coverpoint a {
      wildcard bins low = {2'b0?};
      wildcard bins high = {2'b1?};
    }
    b_point: coverpoint b {
      bins zero = {0};
      bins one = {1};
    }
    c_point: coverpoint c {
      bins zero = {0};
      bins one = {1};
    }
    empty_point: coverpoint empty_value {
      option.auto_bin_max = 0;
    }
    array_point: coverpoint a {
      bins pair[] = {[0:1]};
      bins high = {[2:3]};
    }
    product: cross a_point, b_point {
      option.at_least = 2;
      bins a_zero = binsof(a_point) intersect {selected_value};
    }
    all_product: cross a_point, b_point {
      bins all_a = binsof(a_point);
    }
    empty_intersect_product: cross a_point, b_point {
      bins none = binsof(a_point) intersect {4};
    }
    zero_target_product: cross a_point, empty_point {
      bins none = binsof(a_point) intersect {0};
    }
    named_product: cross a_point, b_point {
      bins named_low = binsof(a_point.low);
    }
    named_intersect_product: cross a_point, b_point {
      // Intersect selects the entire named bin when their value sets overlap;
      // it does not partition the bin's values.
      bins named_high = binsof(a_point.high) intersect {2};
    }
    named_array_product: cross array_point, b_point {
      bins named_pair = binsof(array_point.pair);
    }
    negated_product: cross a_point, b_point {
      bins outside_low = !binsof(a_point) intersect {0};
    }
    negated_named_array_product: cross array_point, b_point {
      bins outside_pair = !binsof(array_point.pair);
    }
    and_product: cross a_point, b_point {
      option.cross_num_print_missing = 2;
      // IEEE 1800-2023 19.6.1.1: each && bin is one Cartesian
      // rectangle. Removing both rectangles leaves the opposite diagonal as
      // retained automatic bins; intersecting disjoint bins of the same
      // target produces an empty bin.
      bins low_one = binsof(a_point.low) && binsof(b_point.one);
      bins high_zero = binsof(a_point.high) && binsof(b_point.zero);
      bins empty = binsof(a_point.low) && binsof(a_point.high);
    }
    or_product: cross a_point, b_point {
      // The union selects three of the four products. The low/one product
      // satisfies both alternatives but increments this bin only once.
      bins low_or_one = binsof(a_point.low) || binsof(b_point.one);
    }
    tautology_product: cross a_point, b_point {
      bins all = binsof(a_point.low) || !binsof(a_point.low);
    }
    empty_or_product: cross a_point, b_point {
      // An empty disjunct does not make the complete OR bin empty.
      bins one = (binsof(a_point.low) && binsof(a_point.high)) ||
                 binsof(b_point.one);
    }
    mixed_product: cross a_point, b_point, c_point {
      bins selected =
          (binsof(a_point.low) || binsof(b_point.one)) && binsof(c_point.zero);
    }
  endgroup

  covergroup retain_policy_cg(input bit retain_default);
    option.cross_retain_auto_bins = retain_default;
    policy_a: coverpoint a {
      wildcard bins low = {2'b0?};
      wildcard bins high = {2'b1?};
    }
    policy_b: coverpoint b {
      bins zero = {0};
      bins one = {1};
    }
    discarded: cross policy_a, policy_b {
      bins selected = binsof(policy_a.low);
    }
    retained: cross policy_a, policy_b {
      option.cross_retain_auto_bins = 1;
      bins selected = binsof(policy_a.low);
    }
    automatic_only: cross policy_a, policy_b;
    empty_selected: cross policy_a, policy_b {
      bins none = binsof(policy_a) intersect {4};
    }
  endgroup

  cg cov;
  cg other;
  retain_policy_cg policy;
  retain_policy_cg policy_keep;
  int covered;
  int total;
  real percentage;

  initial begin
    if ($test$plusargs("reverse")) begin
      other = new(1);
      cov = new(0);
      policy_keep = new(1);
      policy = new(0);
    end else begin
      cov = new(0);
      other = new(1);
      policy = new(0);
      policy_keep = new(1);
    end
    a = 0;
    b = 0;
    cov.sample();
    policy.sample();
    policy_keep.sample();
    b = 1;
    a = 1;
    cov.sample();
    policy.sample();
    policy_keep.sample();
    a = 2;
    b = 0;
    cov.sample();
    policy.sample();
    policy_keep.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("explicit cross %.6f %0d %0d", percentage, covered, total);
    percentage = cg::product::get_coverage(covered, total);
    $display("explicit cross type %.6f %0d %0d", percentage, covered, total);
    percentage = cov.all_product.get_inst_coverage(covered, total);
    $display("all explicit cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.empty_intersect_product.get_inst_coverage(covered, total);
    $display("empty intersect cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.zero_target_product.get_inst_coverage(covered, total);
    $display("zero target cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.named_product.get_inst_coverage(covered, total);
    $display("named cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.named_intersect_product.get_inst_coverage(covered, total);
    $display("named intersect cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.named_array_product.get_inst_coverage(covered, total);
    $display("named array cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.negated_product.get_inst_coverage(covered, total);
    $display("negated cross %.6f %0d %0d", percentage, covered, total);
    percentage =
        cov.negated_named_array_product.get_inst_coverage(covered, total);
    $display("negated named array cross %.6f %0d %0d", percentage, covered,
             total);
    percentage = cov.and_product.get_inst_coverage(covered, total);
    $display("and cross %.6f %0d %0d", percentage, covered, total);
    percentage = cg::and_product::get_coverage(covered, total);
    $display("and cross type %.6f %0d %0d", percentage, covered, total);
    percentage = cov.or_product.get_inst_coverage(covered, total);
    $display("or cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.tautology_product.get_inst_coverage(covered, total);
    $display("tautology cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.empty_or_product.get_inst_coverage(covered, total);
    $display("empty alternative cross %.6f %0d %0d", percentage, covered,
             total);
    percentage = cov.mixed_product.get_inst_coverage(covered, total);
    $display("mixed or-and cross %.6f %0d %0d", percentage, covered, total);
    percentage = policy.discarded.get_inst_coverage(covered, total);
    $display("discard automatic cross %.6f %0d %0d", percentage, covered,
             total);
    percentage = policy.retained.get_inst_coverage(covered, total);
    $display("override retain automatic cross %.6f %0d %0d", percentage,
             covered, total);
    percentage = policy.automatic_only.get_inst_coverage(covered, total);
    $display("discard all automatic cross %.6f %0d %0d", percentage, covered,
             total);
    percentage = policy_keep.discarded.get_inst_coverage(covered, total);
    $display("constructor retain automatic cross %.6f %0d %0d", percentage,
             covered, total);
    percentage = policy_keep.automatic_only.get_inst_coverage(covered, total);
    $display("constructor automatic-only cross %.6f %0d %0d", percentage,
             covered, total);
    percentage = policy.empty_selected.get_inst_coverage(covered, total);
    $display("discard empty explicit cross %.6f %0d %0d", percentage, covered,
             total);
    percentage = policy_keep.empty_selected.get_inst_coverage(covered, total);
    $display("retain empty explicit cross %.6f %0d %0d", percentage, covered,
             total);
    $finish;
  end
endmodule

// QUERY: explicit cross 33.333333 1 3
// QUERY-NEXT: explicit cross type 33.333333 1 3
// QUERY-NEXT: all explicit cross 100.000000 1 1
// QUERY-NEXT: empty intersect cross 75.000000 3 4
// QUERY-NEXT: zero target cross 0.000000 0 0
// QUERY-NEXT: named cross 66.666667 2 3
// QUERY-NEXT: named intersect cross 100.000000 3 3
// QUERY-NEXT: named array cross 66.666667 2 3
// QUERY-NEXT: negated cross 100.000000 3 3
// QUERY-NEXT: negated named array cross 60.000000 3 5
// QUERY-NEXT: and cross 75.000000 3 4
// QUERY-NEXT: and cross type 75.000000 3 4
// QUERY-NEXT: or cross 100.000000 2 2
// QUERY-NEXT: tautology cross 100.000000 1 1
// QUERY-NEXT: empty alternative cross 100.000000 3 3
// QUERY-NEXT: mixed or-and cross 33.333333 2 6
// QUERY-NEXT: discard automatic cross 100.000000 1 1
// QUERY-NEXT: override retain automatic cross 66.666667 2 3
// QUERY-NEXT: discard all automatic cross 0.000000 0 0
// QUERY-NEXT: constructor retain automatic cross 66.666667 2 3
// QUERY-NEXT: constructor automatic-only cross 75.000000 3 4
// QUERY-NEXT: discard empty explicit cross 0.000000 0 0
// QUERY-NEXT: retain empty explicit cross 75.000000 3 4
// LOADED: explicit cross 66.666667 2 3
// LOADED-NEXT: explicit cross type 66.666667 2 3
// LOADED-NEXT: all explicit cross 100.000000 1 1
// LOADED-NEXT: empty intersect cross 75.000000 3 4
// LOADED-NEXT: zero target cross 0.000000 0 0
// LOADED-NEXT: named cross 66.666667 2 3
// LOADED-NEXT: named intersect cross 100.000000 3 3
// LOADED-NEXT: named array cross 66.666667 2 3
// LOADED-NEXT: negated cross 100.000000 3 3
// LOADED-NEXT: negated named array cross 60.000000 3 5
// LOADED-NEXT: and cross 75.000000 3 4
// LOADED-NEXT: and cross type 75.000000 3 4
// LOADED-NEXT: or cross 100.000000 2 2
// LOADED-NEXT: tautology cross 100.000000 1 1
// LOADED-NEXT: empty alternative cross 100.000000 3 3
// LOADED-NEXT: mixed or-and cross 33.333333 2 6
// LOADED-NEXT: discard automatic cross 100.000000 1 1
// LOADED-NEXT: override retain automatic cross 66.666667 2 3
// LOADED-NEXT: discard all automatic cross 0.000000 0 0
// LOADED-NEXT: constructor retain automatic cross 66.666667 2 3
// LOADED-NEXT: constructor automatic-only cross 75.000000 3 4
// LOADED-NEXT: discard empty explicit cross 0.000000 0 0
// LOADED-NEXT: retain empty explicit cross 75.000000 3 4
// REPORT-DAG: cross product: 1/3
// REPORT-DAG: bin a_zero: 2 [covered]
// REPORT-DAG: auto bin <high,zero>: 1 [uncovered, at_least 2]
// REPORT-DAG: cross all_product: 1/1
// REPORT-DAG: automatic: 0/0
// REPORT-DAG: bin all_a: 3 [covered]
// REPORT-DAG: cross empty_intersect_product: 3/4
// REPORT-DAG: cross zero_target_product: 0/0
// REPORT-DAG: cross named_product: 2/3
// REPORT-DAG: bin named_low: 2 [covered]
// REPORT-DAG: cross named_intersect_product: 3/3
// REPORT-DAG: bin named_high: 1 [covered]
// REPORT-DAG: cross named_array_product: 2/3
// REPORT-DAG: bin named_pair: 2 [covered]
// REPORT-DAG: cross negated_product: 3/3
// REPORT-DAG: bin outside_low: 1 [covered]
// REPORT-DAG: cross negated_named_array_product: 3/5
// REPORT-DAG: bin outside_pair: 1 [covered]
// REPORT-DAG: cross and_product: 3/4
// REPORT-DAG: bin low_one: 1 [covered]
// REPORT-DAG: bin high_zero: 1 [covered]
// REPORT-DAG: auto bin <low,zero>: 1 [covered, at_least 1]
// REPORT-DAG: auto bin <high,one>: 0 [uncovered, at_least 1]
// REPORT-DAG: cross or_product: 2/2
// REPORT-DAG: bin low_or_one: 2 [covered]
// REPORT-DAG: auto bin <high,zero>: 1 [covered, at_least 1]
// REPORT-DAG: cross tautology_product: 1/1
// REPORT-DAG: bin all: 3 [covered]
// REPORT-DAG: cross empty_or_product: 3/3
// REPORT-DAG: bin one: 1 [covered]
// REPORT-DAG: cross mixed_product: 2/6
// REPORT-DAG: bin selected: 2 [covered]
// REPORT-DAG: auto bin <high,zero,zero>: 1 [covered, at_least 1]
// REPORT-DAG: cross discarded: 1/1
// REPORT-DAG: cross retained: 2/3
// REPORT-DAG: cross automatic_only: 0/0
// REPORT-DAG: cross automatic_only: 3/4
// REPORT-DAG: cross empty_selected: 0/0
// REPORT-DAG: cross empty_selected: 3/4
