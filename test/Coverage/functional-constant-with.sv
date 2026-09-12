// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=constant-with > %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=constant-with > %t.bytecode.out
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

module top;
  bit [3:0] sampled;

  covergroup cg;
    type_option.distribute_first = 1;
    cp: coverpoint sampled {
      bins keep = {[0:3]} with (1'b1);
      bins drop = {[4:7]} with (1'b0);
      bins drop_unsized[] = {8, 9} with (1'b0);
      bins drop_fixed[2] = {[10:11]} with (1'b0);
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;
  initial begin
    cov = new;
    sampled = 1;
    cov.sample();
    sampled = 5;
    cov.sample();
    sampled = 8;
    cov.sample();
    percentage = cov.cp.get_inst_coverage(covered, total);
    $display("constant with %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: constant with 100.000000 1 1
// REPORT: functional: 100.00% (1/1)
// REPORT-DAG: coverpoint cp: 1/1 (100.00%)
// REPORT-DAG: bin keep: 1 [covered]
// Empty resolved bins are visible but excluded from the denominator.
// REPORT-DAG: bin drop: 0 [excluded]
// `distribute_first` creates the unsized array bins before the false `with`
// predicate empties them (IEEE 1800-2023 19.5.1.1 and 19.5.5).
// REPORT-DAG: bin drop_unsized[8]: 0 [excluded]
// REPORT-DAG: bin drop_unsized[9]: 0 [excluded]
// REPORT-DAG: bin drop_fixed[0]: 0 [excluded]
// REPORT-DAG: bin drop_fixed[1]: 0 [excluded]
