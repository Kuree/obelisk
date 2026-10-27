// XFAIL: *
// RUN: %talos --std=1800-2023 --target=native --coverage=functional -o %t %s
// RUN: %t | FileCheck %s

// IEEE 19.8.1 permits typed sample arguments with default values. Pristine
// Slang v11.0 fails to resolve the compilation-unit class name here.
class Record; int number; endclass
module top;
  covergroup cg with function sample(input Record value = null);
    cp: coverpoint (value == null) { bins nil = {1}; }
  endgroup
  cg group_value;
  initial begin
    group_value = new;
    group_value.sample();
    $display("coverage=%0.0f", group_value.get_inst_coverage());
    $finish;
  end
endmodule
// CHECK: coverage=100
