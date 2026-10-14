// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t 2> %t.err | FileCheck %s
// RUN: FileCheck %s --check-prefix=ERROR < %t.err

// IEEE 1800-2023 19.5, 19.6.2 and 19.6.3: a false bin iff only stops that
// bin's count. Guarded ignore and illegal cross bins still exclude their
// products, so the explicit bin `high` cannot count the ignored product and a
// disabled illegal bin reports nothing. Once enabled, the illegal bin reports
// exactly once.
// CHECK: disabled=0.00
// CHECK-NEXT: enabled=50.00
// ERROR-COUNT-1: ERROR: functional coverage illegal bin 'top.cg.x.bad' sampled
// ERROR-NOT: illegal bin

module top;
  bit a, b, en;
  covergroup cg;
    ca: coverpoint a { bins lo = {0}; bins hi = {1}; }
    cb: coverpoint b { bins lo = {0}; bins hi = {1}; }
    x: cross ca, cb {
      bins high = binsof(ca.hi);
      ignore_bins skip = binsof(ca.hi) && binsof(cb.lo) iff (en);
      illegal_bins bad = binsof(ca.lo) && binsof(cb.hi) iff (en);
    }
  endgroup
  cg cov = new;
  initial begin
    en = 0;
    a = 1; b = 0; cov.sample();
    a = 0; b = 1; cov.sample();
    $display("disabled=%0.2f", cov.x.get_inst_coverage());
    en = 1;
    a = 0; b = 1; cov.sample();
    a = 1; b = 1; cov.sample();
    $display("enabled=%0.2f", cov.x.get_inst_coverage());
    $finish;
  end
endmodule
