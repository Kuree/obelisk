// IEEE 1800-2023 19.5.2 makes a one-item transition whose repetition range
// includes one illegal because that expansion has transition length zero.
module top;
  bit [3:0] sampled;

  covergroup cg(input int lower, upper);
    cp: coverpoint sampled {
      bins paths[] = (3 [* lower:upper]);
    }
  endgroup

  cg cov;
  initial begin
    cov = new(1, 2);
    $finish;
  end
endmodule

// CHECK: error: simulation ended: invalid design metadata (status 16)
// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: not %t.native --no-coverage-dump 2>&1 | FileCheck %s
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: not %t.bytecode --no-coverage-dump 2>&1 | FileCheck %s
