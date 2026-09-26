// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t %s
// RUN: not %t --no-coverage-dump 2>&1 | FileCheck %s

// The v1 physical schema represents a resolved bin-group cardinality in
// uint32_t.  A 32-bit domain with no defined values therefore cannot be
// materialized as one default-array bin per value and must fail explicitly.
module top;
  bit [31:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins every_value[] = default;
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    $finish;
  end
endmodule

// CHECK: simulation ended: out of resources (status 6)
