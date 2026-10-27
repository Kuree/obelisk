// XFAIL: *
// RUN: %talos --std=1800-2023 -O0 --target=native --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 1800-2023 13.5.3 resolves defaults in the declaration's enclosing scope.
// Slang v11.0 reports the module's variable and function as undeclared in a
// custom sample method default. Keep the legal source without a lookup patch.
module top;
  int default_value = 1;
  function automatic int bump(inout int target);
    target++;
    return 0;
  endfunction
  covergroup cg with function sample(ref int live = default_value,
                                     input int ignored = bump(default_value));
    cp: coverpoint live { bins two = {2}; }
  endgroup
  cg cov;
  int covered, total;
  real percentage;
  initial begin
    cov = new;
    cov.sample();
    percentage = cov.cp.get_inst_coverage(covered, total);
    $display("default %0d %.0f %0d/%0d", default_value, percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: default 2 100 1/1
