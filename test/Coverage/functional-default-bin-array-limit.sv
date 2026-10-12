// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --no-coverage-dump 2>&1 | FileCheck %s
// RUN: not %t +automatic --no-coverage-dump 2>&1 | FileCheck %s

// The physical schema has a maximum record count below UINT32_MAX. Both
// paths must reject larger expansions before allocating/enumerating values.
module top;
  bit [30:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins every_value[] = default;
    }
  endgroup
  covergroup automatic_cg;
    cp: coverpoint sampled { option.auto_bin_max = 536870912; }
  endgroup

  cg cov;
  automatic_cg automatic_cov;
  initial begin
    if ($test$plusargs("automatic"))
      automatic_cov = new;
    else
      cov = new;
    $finish;
  end
endmodule

// CHECK: simulation ended: out of resources (status 6)
