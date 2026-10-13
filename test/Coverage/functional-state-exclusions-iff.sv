// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.5 stops a bin's count when its iff is false, while
// 19.5.5-19.5.6 remove excluded values from every bin whatever the iff says.
// Integral, wildcard, real and shortreal exclusions keep the same bins and
// denominators with the guards off and on; only an enabled illegal guard
// reports.
module top;
  logic [1:0] value;
  real wide;
  shortreal narrow;
  bit enable_ignore, enable_illegal;
  int covered, total;
  real percentage;
  covergroup cg;
    cp: coverpoint value {
      bins cp_whole = {[0:3]};
      bins cp_each[] = {[0:3]};
      bins cp_one = {1};
      ignore_bins cp_removed = {1} iff (enable_ignore);
      illegal_bins cp_bad = {2} iff (enable_illegal);
      bins cp_fallback = default;
    }
    wp: coverpoint value {
      bins wp_whole = {[0:3]};
      bins wp_each[] = {[0:3]};
      wildcard ignore_bins wp_removed = {2'b0?} iff (enable_ignore);
    }
    rp: coverpoint wide {
      bins rp_whole = {[0.0:3.0]};
      bins rp_at_one = {1.0};
      bins rp_at_two = {2.0};
      ignore_bins rp_removed = {1.0} iff (enable_ignore);
      illegal_bins rp_bad = {2.0} iff (enable_illegal);
    }
    sp: coverpoint narrow {
      bins sp_whole = {[0.0:3.0]};
      bins sp_at_one = {1.0};
      bins sp_at_two = {2.0};
      ignore_bins sp_removed = {1.0} iff (enable_ignore);
      illegal_bins sp_bad = {2.0} iff (enable_illegal);
    }
    product: cross cp, wp;
  endgroup
  cg cov;
  task automatic take(input logic [1:0] v, input real r);
    value = v;
    wide = r;
    narrow = r;
    cov.sample();
  endtask
  task automatic sweep();
    take(0, 0.5); take(1, 1.0); take(2, 2.0); take(3, 3.0);
  endtask
  task automatic show(input string phase);
    percentage = cov.cp.get_inst_coverage(covered, total);
    $display("%s cp %0d/%0d", phase, covered, total);
    percentage = cov.wp.get_inst_coverage(covered, total);
    $display("%s wp %0d/%0d", phase, covered, total);
    percentage = cov.rp.get_inst_coverage(covered, total);
    $display("%s rp %0d/%0d", phase, covered, total);
    percentage = cov.sp.get_inst_coverage(covered, total);
    $display("%s sp %0d/%0d", phase, covered, total);
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("%s product %0d/%0d", phase, covered, total);
  endtask
  initial begin
    cov = new;
    show("empty");
    sweep();
    take('x, 0.5);
    show("guards-off");
    enable_ignore = 1;
    enable_illegal = 1;
    sweep();
    show("guards-on");
    $finish;
  end
endmodule

// Empty bins leave the denominators before any sample: cp keeps its whole bin
// and the values 0 and 3, wp keeps 2 and 3, the real bins keep only the range,
// and the cross has 3 x 3 bins because default and excluded bins are not
// crossed. Sampling the excluded values with false guards covers nothing more.
// OUT: empty cp 0/3
// OUT-NEXT: empty wp 0/3
// OUT-NEXT: empty rp 0/1
// OUT-NEXT: empty sp 0/1
// OUT-NEXT: empty product 0/9
// OUT-NEXT: guards-off cp 3/3
// OUT-NEXT: guards-off wp 3/3
// OUT-NEXT: guards-off rp 1/1
// OUT-NEXT: guards-off sp 1/1
// OUT-NEXT: guards-off product 4/9
// OUT-NEXT: guards-on cp 3/3
// OUT-NEXT: guards-on wp 3/3
// OUT-NEXT: guards-on rp 1/1
// OUT-NEXT: guards-on sp 1/1
// OUT-NEXT: guards-on product 4/9

// Each excluded value is missing from the counts in both phases: cp_whole
// counts 0 and 3 twice, the unmatched X goes to the default bin, wp_whole
// counts only 2 and 3, and the real ranges skip 1.0 and 2.0. The cross hits
// only the 3-3 combinations, once per enabled sweep.
// REPORT: functional: 88.89% (12/17)
// REPORT-DAG: coverpoint cp: 3/3 (100.00%)
// REPORT-DAG: bin cp_whole: 4 [covered]
// REPORT-DAG: bin cp_each[0]: 2 [covered]
// REPORT-DAG: bin cp_each[1]: 0 [excluded] {empty}
// REPORT-DAG: bin cp_each[2]: 0 [excluded] {empty}
// REPORT-DAG: bin cp_each[3]: 2 [covered]
// REPORT-DAG: bin cp_one: 0 [excluded] {empty}
// REPORT-DAG: bin cp_removed: 0 [excluded] {ignore}
// REPORT-DAG: bin cp_bad: 0 [excluded] {illegal}
// REPORT-DAG: bin cp_fallback: 1 [excluded] {default}
// REPORT-DAG: coverpoint wp: 3/3 (100.00%)
// REPORT-DAG: bin wp_whole: 4 [covered]
// REPORT-DAG: bin wp_each[0]: 0 [excluded] {empty}
// REPORT-DAG: bin wp_each[1]: 0 [excluded] {empty}
// REPORT-DAG: bin wp_each[2]: 2 [covered]
// REPORT-DAG: bin wp_each[3]: 2 [covered]
// REPORT-DAG: bin wp_removed: 0 [excluded] {ignore, wildcard}
// REPORT-DAG: coverpoint rp: 1/1 (100.00%)
// REPORT-DAG: bin rp_whole: 5 [covered]
// REPORT-DAG: bin rp_at_one: 0 [excluded] {empty}
// REPORT-DAG: bin rp_at_two: 0 [excluded] {empty}
// REPORT-DAG: bin rp_removed: 0 [excluded] {ignore}
// REPORT-DAG: bin rp_bad: 0 [excluded] {illegal}
// REPORT-DAG: coverpoint sp: 1/1 (100.00%)
// REPORT-DAG: bin sp_whole: 5 [covered]
// REPORT-DAG: bin sp_at_one: 0 [excluded] {empty}
// REPORT-DAG: bin sp_at_two: 0 [excluded] {empty}
// REPORT-DAG: bin sp_removed: 0 [excluded] {ignore}
// REPORT-DAG: bin sp_bad: 0 [excluded] {illegal}
// REPORT-DAG: cross product: 4/9 (44.44%)
// REPORT-DAG: auto bin <cp_whole,wp_whole>: 2 [covered, at_least 1]
// REPORT-DAG: auto bin <cp_whole,wp_each[3]>: 2 [covered, at_least 1]
// REPORT-DAG: auto bin <cp_each[3],wp_whole>: 2 [covered, at_least 1]
// REPORT-DAG: auto bin <cp_each[3],wp_each[3]>: 2 [covered, at_least 1]

// Only the enabled illegal guards report, once each for value 2 and 2.0.
// REPORT: illegal-bin diagnostics:
// REPORT-DAG: top.cg.cp.cp_bad instance 1 test "default": 1 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.rp.rp_bad instance 1 test "default": 1 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.sp.sp_bad instance 1 test "default": 1 at time 0 - "illegal bin sampled"
