// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: not %t.native --no-coverage-dump 2>&1 | FileCheck %s
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: not %t.bytecode --no-coverage-dump 2>&1 | FileCheck %s

// IEEE 1800-2023 19.5.2 makes a single repeated item with a repetition-count-1
// expansion illegal because that expansion contains no transition.  The
// constructor arguments make this a resolved-instance check rather than a
// static frontend check.
module top;
  bit [2:0] sampled;
  covergroup cg(int low, int high);
    cp: coverpoint sampled {
      bins invalid = (5 [* low:high]);
    }
  endgroup

  cg cov;
  initial begin
    cov = new(1, 2);
    sampled = 5;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: error: simulation ended: invalid design metadata (status 16)
