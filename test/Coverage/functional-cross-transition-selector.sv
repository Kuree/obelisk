// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=cross-transition > %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=cross-transition > %t.bytecode.out
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

// IEEE 1800-2023 19.6.1.1: binsof() uses the last value of a transition
// when a cross-bin select expression is applied to transition bins.
module top;
  bit transition_value;
  bit side_value;

  covergroup cg;
    transition_point: coverpoint transition_value {
      bins rise = (0 => 1);
      bins fall = (1 => 0);
    }
    array_point: coverpoint transition_value {
      bins paths[] = (0 => 1), (1 => 0);
    }
    suppressed_point: coverpoint transition_value {
      bins alternatives = (0 => 1), (1 => 0);
      ignore_bins remove_rise = (0 => 1);
    }
    side_point: coverpoint side_value {
      bins zero = {0};
      bins one = {1};
    }
    product: cross transition_point, side_point {
      bins selected = binsof(transition_point) intersect {1} &&
                      binsof(side_point.zero);
    }
    array_product: cross array_point, side_point {
      bins selected = binsof(array_point) intersect {1} &&
                      binsof(side_point.zero);
    }
    suppressed_product: cross suppressed_point, side_point {
      bins empty = binsof(suppressed_point) intersect {1};
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    transition_value = 0;
    side_value = 0;
    cov.sample();
    transition_value = 1;
    cov.sample();
    transition_value = 0;
    side_value = 1;
    cov.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("transition cross %.6f %0d %0d", percentage, covered, total);
    percentage = cov.array_product.get_inst_coverage(covered, total);
    $display("transition array cross %.6f %0d %0d", percentage, covered,
             total);
    percentage = cov.suppressed_product.get_inst_coverage(covered, total);
    $display("suppressed transition cross %.6f %0d %0d", percentage, covered,
             total);
    $finish;
  end
endmodule

// QUERY: transition cross 50.000000 2 4
// QUERY-NEXT: transition array cross 50.000000 2 4
// QUERY-NEXT: suppressed transition cross 50.000000 1 2
// REPORT-DAG: cross product: 2/4
// REPORT-DAG: bin selected: 1 [covered]
// REPORT-DAG: auto bin <fall,one>: 1 [covered, at_least 1]
// REPORT-DAG: cross array_product: 2/4
// REPORT-DAG: auto bin <paths[1=>0],one>: 1 [covered, at_least 1]
// REPORT-DAG: cross suppressed_product: 1/2
// REPORT-DAG: auto bin <alternatives,one>: 1 [covered, at_least 1]
