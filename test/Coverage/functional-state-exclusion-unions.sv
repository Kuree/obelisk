// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.5.5 removes the union of ignored values from every bin
// after the values are distributed to the bins, so an ignore array whose
// pieces cover a bin leaves it empty and a bin larger than the union keeps
// the rest. Real partitions are closed at the end of the final one only, and a
// wildcard pair covering the whole domain empties its bin. The ignore guard
// does not matter (19.5).
module top;
  logic signed [64:0] wide_integer;
  logic [7:0] value;
  real wide;
  shortreal narrow;
  bit enabled;
  int covered, total;
  real percentage;
  covergroup cg;
    integral_point: coverpoint wide_integer {
      bins i_empty = {[-16:15]};
      bins i_partial = {[-16:16]};
      ignore_bins i_pieces[] = {[-16:15]} iff (enabled);
    }
    real_point: coverpoint wide {
      bins r_empty = {[0.0:4.0]};
      bins r_partial = {[0.0:5.0]};
      bins r_parts[4] = {[0.0:8.0]};
      ignore_bins r_pieces[] = {[0.0:4.0]} iff (enabled);
    }
    short_point: coverpoint narrow {
      bins s_empty = {[0.0:4.0]};
      bins s_partial = {[0.0:5.0]};
      ignore_bins s_pieces[] = {[0.0:4.0]} iff (enabled);
    }
    wildcard_point: coverpoint value {
      bins w_empty = {[0:255]};
      wildcard ignore_bins prefix_low = {8'b0???????} iff (enabled);
      wildcard ignore_bins prefix_high = {8'b1???????} iff (enabled);
    }
  endgroup
  cg cov;
  task automatic take(input logic signed [64:0] i, input logic [7:0] v,
                      input real r);
    wide_integer = i;
    value = v;
    wide = r;
    narrow = r;
    cov.sample();
  endtask
  task automatic show(input string phase);
    percentage = cov.integral_point.get_inst_coverage(covered, total);
    $display("%s integral %0d/%0d", phase, covered, total);
    percentage = cov.real_point.get_inst_coverage(covered, total);
    $display("%s real %0d/%0d", phase, covered, total);
    percentage = cov.short_point.get_inst_coverage(covered, total);
    $display("%s short %0d/%0d", phase, covered, total);
    percentage = cov.wildcard_point.get_inst_coverage(covered, total);
    $display("%s wildcard %0d/%0d", phase, covered, total);
  endtask
  initial begin
    cov = new;
    show("empty");
    take(5, 8'h05, 2.0);
    take(15, 8'h80, 4.0);
    take(-16, 8'hff, 3.999);
    take(16, 8'h00, 4.5);
    take(-17, 8'h7f, 5.0);
    take(17, 8'h10, 5.5);
    take(1, 1, 5.9);
    take(1, 1, 6.0);
    take(1, 1, 8.0);
    take(1, 1, 8.5);
    show("guard-off");
    enabled = 1;
    take(16, 8'h00, 4.5);
    take(5, 1, 2.0);
    show("guard-on");
    $finish;
  end
endmodule

// Before any sample the denominators already exclude the emptied bins:
// 16 and (4, 5] are what remain of the partial bins, r_parts keeps its last two
// partitions, and the wildcard point has nothing left.
// OUT: empty integral 0/1
// OUT-NEXT: empty real 0/3
// OUT-NEXT: empty short 0/1
// OUT-NEXT: empty wildcard 0/0
// OUT-NEXT: guard-off integral 1/1
// OUT-NEXT: guard-off real 3/3
// OUT-NEXT: guard-off short 1/1
// OUT-NEXT: guard-off wildcard 0/0
// OUT-NEXT: guard-on integral 1/1
// OUT-NEXT: guard-on real 3/3
// OUT-NEXT: guard-on short 1/1
// OUT-NEXT: guard-on wildcard 0/0

// Only 16 reaches i_partial (twice). In the real point 4.0 and 3.999 fall in
// the ignored [0, 4], 4.5 and 5.0 reach r_partial and 5.5 does not, r_parts[2]
// is open at 6.0 so that value starts r_parts[3] ([6, 8], with 8.0 inside and
// 8.5 outside). shortreal behaves like real.
// REPORT: functional: 100.00% (5/5)
// REPORT-DAG: coverpoint integral_point: 1/1 (100.00%)
// REPORT-DAG: bin i_empty: 0 [excluded] {empty}
// REPORT-DAG: bin i_partial: 2 [covered]
// REPORT-DAG: bin i_pieces[-16]: 0 [excluded] {ignore}
// REPORT-DAG: bin i_pieces[15]: 0 [excluded] {ignore}
// REPORT-DAG: coverpoint real_point: 3/3 (100.00%)
// REPORT-DAG: bin r_empty: 0 [excluded] {empty}
// REPORT-DAG: bin r_partial: 3 [covered]
// REPORT-DAG: bin r_parts[0]: 0 [excluded] {empty}
// REPORT-DAG: bin r_parts[1]: 0 [excluded] {empty}
// REPORT-DAG: bin r_parts[2]: 5 [covered]
// REPORT-DAG: bin r_parts[3]: 2 [covered]
// REPORT-DAG: bin r_pieces[0.0:1.0): 0 [excluded] {ignore}
// REPORT-DAG: bin r_pieces[3.0:4.0]: 0 [excluded] {ignore}
// REPORT-DAG: coverpoint short_point: 1/1 (100.00%)
// REPORT-DAG: bin s_empty: 0 [excluded] {empty}
// REPORT-DAG: bin s_partial: 3 [covered]
// REPORT-DAG: bin s_pieces[0.0:1.0): 0 [excluded] {ignore}
// REPORT-DAG: bin s_pieces[3.0:4.0]: 0 [excluded] {ignore}
// REPORT-DAG: coverpoint wildcard_point: 0/0 (0.00%)
// REPORT-DAG: bin w_empty: 0 [excluded] {empty}
// REPORT-DAG: bin prefix_low: 0 [excluded] {ignore, wildcard}
// REPORT-DAG: bin prefix_high: 0 [excluded] {ignore, wildcard}
