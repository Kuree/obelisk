// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.6.2-19.6.3 and 19.11.2: ignored and illegal products are
// excluded even from other user-defined cross bins, so a bin whose products
// are all ignored or illegal is empty and leaves the denominator (Bu), while a
// partly ignored bin keeps its remaining products. Both the Cartesian (rect)
// and the symbolic `with` (sym) selections follow this rule; sym keeps one
// automatic product, <lo,hi>.
// CHECK: rect 100.00
// CHECK-NEXT: sym 50.00
// REPORT: cross rect: 1/1 (100.00%)
// REPORT-NEXT: automatic: 0/0
// REPORT-NEXT: bin whole: 0 [excluded] {empty}
// REPORT-NEXT: bin part: 1 [covered]
// REPORT-NEXT: bin guarded: 0 [excluded] {empty}
// REPORT: cross sym: 1/2 (50.00%)
// REPORT-NEXT: automatic: 0/1
// REPORT-NEXT: bin whole: 0 [excluded] {empty}
// REPORT-NEXT: bin part: 1 [covered]

module top;
  bit [1:0] a, b;
  covergroup cg;
    ca: coverpoint a { bins lo = {0}; bins hi = {1}; }
    cb: coverpoint b { bins lo = {0}; bins hi = {1}; }
    rect: cross ca, cb {
      bins whole = binsof(ca.hi);
      bins part = binsof(cb.lo);
      bins guarded = binsof(ca.lo) && binsof(cb.hi);
      ignore_bins skip = binsof(ca.hi);
      illegal_bins bad = binsof(ca.lo) && binsof(cb.hi);
    }
    sym: cross ca, cb {
      bins whole = binsof(ca) with (ca == 1);
      bins part = binsof(cb) with (cb == 0);
      ignore_bins skip = binsof(ca) with (ca == 1);
    }
  endgroup
  cg cov = new;
  initial begin
    a = 0; b = 0; cov.sample();
    $display("rect %0.2f", cov.rect.get_inst_coverage());
    $display("sym %0.2f", cov.sym.get_inst_coverage());
    $finish;
  end
endmodule
