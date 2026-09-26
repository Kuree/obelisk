// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.forward.obcov \
// RUN:   --coverage-test=cross-forward +producer
// RUN: %t.native --coverage-output=%t.native.reverse.obcov \
// RUN:   --coverage-test=cross-reverse +producer +reverse
// RUN: obelisk-cov merge -o %t.native.orders.obcov \
// RUN:   %t.native.forward.obcov %t.native.reverse.obcov
// RUN: %t.native --coverage-load=%t.native.forward.obcov \
// RUN:   --coverage-output=%t.native.loaded.obcov \
// RUN:   --coverage-test=cross-loaded > %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.forward.obcov \
// RUN:   --coverage-test=cross-forward +producer
// RUN: %t.bytecode --coverage-output=%t.bytecode.reverse.obcov \
// RUN:   --coverage-test=cross-reverse +producer +reverse
// RUN: obelisk-cov merge -o %t.bytecode.orders.obcov \
// RUN:   %t.bytecode.forward.obcov %t.bytecode.reverse.obcov
// RUN: %t.bytecode --coverage-load=%t.bytecode.forward.obcov \
// RUN:   --coverage-output=%t.bytecode.loaded.obcov \
// RUN:   --coverage-test=cross-loaded > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=text %t.native.loaded.obcov -o %t.report
// RUN: FileCheck %s --check-prefix=REPORT < %t.report

module top;
  bit a;
  bit b;

  covergroup configured(input int count);
    type_option.merge_instances = 1;
    option.get_inst_coverage = 0;
    a_point: coverpoint a {
      option.auto_bin_max = count;
    }
    b_point: coverpoint b;
    product: cross a_point, b_point;
  endgroup

  covergroup correlated(input bit selected_b);
    type_option.merge_instances = 1;
    a_point: coverpoint a {
      bins zero = {0};
      bins one = {1};
    }
    b_point: coverpoint b {
      bins zero = {0};
      bins one = {1};
    }
    product: cross a_point, b_point {
      // Configuration 0 removes only <one,zero>; configuration 1 removes
      // <one,zero> and <one,one>. Their exact automatic-bin union therefore
      // has three tuples. A Cartesian-marginal approximation incorrectly
      // reconstructs all four tuples.
      bins selected = binsof(a_point.one) &&
                      binsof(b_point) intersect {[0:selected_b]};
    }
  endgroup

  configured empty;
  configured one;
  configured two;
  correlated correlated_zero;
  correlated correlated_one;
  int covered;
  int total;
  real percentage;

  initial begin
    // Construct the zero-denominator configuration before and after the
    // nonempty configurations in separate runs. Their schema fingerprints
    // must remain identical, which the merge commands above verify.
    if ($test$plusargs("reverse")) begin
      correlated_one = new(1);
      correlated_zero = new(0);
      two = new(2);
      one = new(1);
      empty = new(0);
    end else begin
      empty = new(0);
      one = new(1);
      two = new(2);
      correlated_zero = new(0);
      correlated_one = new(1);
    end

    if ($test$plusargs("producer")) begin
      a = 0;
      b = 0;
      one.sample();
      correlated_zero.sample();
      a = 1;
      correlated_zero.sample();
      $finish;
    end

    // The loaded one-bin configuration contributes <auto[0:1],auto[0]>;
    // this live two-bin configuration contributes <auto[1],auto[1]>.
    // Union-by-name has six tuples, not the sum of the two products.
    a = 1;
    b = 1;
    two.sample();
    percentage = empty.product.get_inst_coverage(covered, total);
    $display("merged cross %.6f %0d %0d", percentage, covered, total);
    percentage = configured::product::get_coverage(covered, total);
    $display("merged cross type %.6f %0d %0d", percentage, covered, total);
    a = 1;
    b = 1;
    // <one,one> is automatic only in configuration 0. This exercises exact
    // graph membership as well as exact union counting.
    correlated_zero.sample();
    b = 0;
    correlated_one.sample();
    percentage = correlated::product::get_coverage(covered, total);
    $display("correlated cross type %.6f %0d %0d", percentage, covered,
             total);
    $finish;
  end
endmodule

// QUERY: merged cross 33.333333 2 6
// QUERY-NEXT: merged cross type 33.333333 2 6
// QUERY-NEXT: correlated cross type 75.000000 3 4
// REPORT: type configured ({{[0-9]+}}): 66.67%
// REPORT-DAG: cross product: 0/0
// REPORT-DAG: cross product: 1/2
// REPORT-DAG: cross product: 1/4
// REPORT: type correlated ({{[0-9]+}}): 91.67%
