// XFAIL: *
// RUN: %talos --std=1800-2023 -O0 --target=native --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 1800-2023 7.10.1 and 7.10.5 limit a [$:N] queue to N+1 entries.
// Slang v11.0's ASTContext::evalDimension requires a positive bound and
// rejects [$:0], which has one legal entry. Preserve the legal declaration.
module top;
  bit [7:0] values[$:0];
  covergroup cg(ref bit [7:0] live[$:0]);
    size: coverpoint live.size() { bins one = {1}; }
    first: coverpoint live[0] { bins one = {1}; }
  endgroup
  cg group_value;
  initial begin
    values.push_back(1);
    values.push_back(2);
    group_value = new(values);
    group_value.sample();
    $display("size=%0d first=%0d coverage=%0.0f", values.size(), values[0],
             group_value.get_inst_coverage());
    $finish;
  end
endmodule
// CHECK: size=1 first=1 coverage=100
