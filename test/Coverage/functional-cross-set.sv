// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=cross-set > %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=cross-set > %t.bytecode.out
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

module top;
  logic [1:0] a;
  logic [1:0] b;

  covergroup cg(int required_matches);
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a {
      bins low = {[0:1]};
      bins high = {[2:3]};
    }
    cb: coverpoint b {
      bins low = {[0:1]};
      bins high = {[2:3]};
    }
    product: cross ca, cb {
      bins default_one = '{'{0, 0}, '{3, 3}};
      bins dynamic_two = '{'{0, 1}, '{1, 0}} matches required_matches;
      bins all_low = '{'{0, 0}, '{0, 1}, '{1, 0}, '{1, 1}} matches $;
      bins filtered = ('{'{0, 0}, '{1, 1}} matches 2)
          with (ca == cb) matches 2;
      bins parent_rejects = ('{'{0, 1}} matches 1)
          with (ca == cb) matches 3;
      bins child_rejects = ('{'{0, 0}} matches 2)
          with (ca == cb) matches 2;
      bins distinct_policies =
          ('{'{0, 0}, '{0, 1}, '{1, 0}, '{1, 1}} matches $)
          with (ca == cb) matches 2;
    }
  endgroup

  cg cov;
  logic [1:0] x;
  logic [1:0] y;

  covergroup four_state_cg;
    option.cross_retain_auto_bins = 0;
    cx: coverpoint x {
      bins exact_x = {2'b0x};
      bins exact_z = {2'b0z};
      wildcard bins known = {2'b0?};
    }
    cy: coverpoint y {
      bins exact_x = {2'b0x};
      bins exact_z = {2'b0z};
      wildcard bins known = {2'b0?};
    }
    four_state_product: cross cx, cy {
      bins exact_xz = '{'{2'b0x, 2'b0z}};
      bins exact_zx = '{'{2'b0z, 2'b0x}};
      bins known_two = '{'{2'b00, 2'b00}, '{2'b01, 2'b01}} matches 2;
      bins known_all = '{'{2'b00, 2'b00}, '{2'b00, 2'b01},
                         '{2'b01, 2'b00}, '{2'b01, 2'b01}} matches $;
    }
  endgroup

  four_state_cg four_state_cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new(2);
    four_state_cov = new;
    a = 0;
    b = 0;
    cov.sample();
    x = 2'b0x;
    y = 2'b0z;
    four_state_cov.sample();
    x = 2'b0z;
    y = 2'b0x;
    four_state_cov.sample();
    x = 2'b00;
    y = 2'b00;
    four_state_cov.sample();
    a = 3;
    b = 3;
    cov.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("cross set %.6f %0d %0d", percentage, covered, total);
    percentage = four_state_cov.four_state_product.get_inst_coverage(
        covered, total);
    $display("cross set four-state %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: cross set 100.000000 5 5
// IEEE 1800-2023 19.5.7 removes non-wildcard singleton bin expressions that
// contain X or Z after warning. The queue still exercises exact X/Z transport;
// those tuples cannot select the retained known-only coverpoint bin tuples.
// QUERY: cross set four-state 100.000000 2 2
// REPORT-DAG: bin exact_xz: 0 [excluded]
// REPORT-DAG: bin exact_zx: 0 [excluded]
// REPORT-DAG: bin known_two: 1 [covered]
// REPORT-DAG: bin known_all: 1 [covered]
// REPORT: cross product: 5/5
// REPORT-DAG: bin default_one: 2 [covered]
// REPORT-DAG: bin dynamic_two: 1 [covered]
// REPORT-DAG: bin all_low: 1 [covered]
// REPORT-DAG: bin filtered: 1 [covered]
// REPORT-DAG: bin parent_rejects: 0 [excluded]
// REPORT-DAG: bin child_rejects: 0 [excluded]
// REPORT-DAG: bin distinct_policies: 1 [covered]
