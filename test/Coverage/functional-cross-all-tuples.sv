// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=cross-all-tuples | FileCheck %s --check-prefix=QUERY
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.6.1.2: the enclosing cross identifier selects every
// possible bin tuple. All tuples therefore belong to one explicit bin and no
// automatically generated cross bin remains.
module top;
  bit a;
  bit b;

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
      bins all = product;
    }
    all_or_product: cross a_point, b_point {
      bins all = all_or_product || binsof(a_point.zero);
    }
    restricted_product: cross a_point, b_point {
      bins zero = restricted_product && binsof(a_point.zero);
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    a = 0;
    b = 0;
    cov.sample();
    a = 1;
    b = 1;
    cov.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("all tuples %.6f %0d %0d", percentage, covered, total);
    percentage = cov.all_or_product.get_inst_coverage(covered, total);
    $display("all tuples or %.6f %0d %0d", percentage, covered, total);
    percentage = cov.restricted_product.get_inst_coverage(covered, total);
    $display("all tuples and %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: all tuples 100.000000 1 1
// QUERY-NEXT: all tuples or 100.000000 1 1
// QUERY-NEXT: all tuples and 66.666667 2 3
// REPORT-DAG: cross product: 1/1
// REPORT-DAG: automatic: 0/0
// REPORT-DAG: bin all: 2 [covered]
// REPORT-DAG: cross all_or_product: 1/1
// REPORT-DAG: cross restricted_product: 2/3
// REPORT-DAG: bin zero: 1 [covered]
// REPORT-DAG: auto bin <one,one>: 1 [covered, at_least 1]
