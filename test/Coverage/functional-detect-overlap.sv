// RUN: %obelisk -fno-lto --std=1800-2017 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=detect-overlap > %t.native.out 2> %t.native.err
// RUN: %obelisk -fno-lto --std=1800-2017 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=detect-overlap > %t.bytecode.out 2> %t.bytecode.err
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: %python -c "import sys; lines=[l for l in open(sys.argv[1]) if 'functional coverpoint' in l]; assert len(lines) == 4; assert sum(\"cg.defaulted'\" in l for l in lines) == 1; assert sum(\"cg.forced'\" in l for l in lines) == 2; assert sum(\"cg.quiet'\" in l for l in lines) == 0; assert sum(\"large_cg.huge_point'\" in l for l in lines) == 1; assert all(' overlap' in l for l in lines)" %t.native.err

module top;
  bit [1:0] sampled;

  covergroup cg(input bit detect);
    option.detect_overlap = detect;
    defaulted: coverpoint sampled {
      bins low = {[0:1]};
      bins high = {[1:2]};
    }
    quiet: coverpoint sampled {
      option.detect_overlap = 0;
      bins low = {[0:1]};
      bins high = {[1:2]};
    }
    forced: coverpoint sampled {
      option.detect_overlap = 1;
      wildcard bins low = {2'b0?};
      wildcard bins high = {2'b?1};
    }
  endgroup

  // This creates 32K physical bins. A pairwise implementation performs over
  // half a billion comparisons; the bounded prefix search reaches the first
  // deterministic overlap after a linear number of candidate visits.
  bit [13:0] wide_sampled;
  covergroup large_cg;
    huge_point: coverpoint wide_sampled {
      option.detect_overlap = 1;
      bins left[] = {[0:16383]};
      bins right[] = {[0:16383]};
    }
  endgroup

  cg disabled;
  cg enabled;
  large_cg large_cov;

  initial begin
    disabled = new(0);
    enabled = new(1);
    large_cov = new;
    $finish;
  end
endmodule
