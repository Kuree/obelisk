// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=real-cross > %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=real-cross > %t.bytecode.out
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

// IEEE 1800-2023 19.5 and 19.6 permit named coverpoints of real
// expressions in crosses. The real value contributes its containing bin to
// the sparse Cartesian tuple just like an integral coverpoint.
module top;
  real sampled;
  bit side;

  covergroup cg;
    real_point: coverpoint sampled {
      bins low = {[0.0:1.0]};
      bins high = {[2.0:3.0]};
    }
    side_point: coverpoint side;
    product: cross real_point, side_point;
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    sampled = 0.5;
    side = 0;
    cov.sample();
    sampled = 2.5;
    side = 1;
    cov.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("real cross %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: real cross 50.000000 2 4
// REPORT-DAG: coverpoint real_point: 2/2 (100.00%)
// REPORT-DAG: cross product: 2/4 (50.00%)
// REPORT-DAG: automatic: 2/4
// REPORT-DAG: auto bin <low,auto[0]>: 1 [covered, at_least 1]
// REPORT-DAG: auto bin <high,auto[1]>: 1 [covered, at_least 1]
