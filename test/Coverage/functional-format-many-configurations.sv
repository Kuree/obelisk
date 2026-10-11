// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// Each of 16 array elements is a separately constructed covergroup with its
// own resolved configuration (IEEE 1800-2023 19.3, 19.8.1), and the codec
// writes and reads all of them.

module top;
  covergroup cg(input int key) with function sample(input int value);
    option.per_instance = 1;
    cp: coverpoint value { bins selected = {key}; }
  endgroup
  cg groups[16];
  int covered, total;
  real result;
  initial begin
    for (int i = 0; i < 16; ++i) begin
      groups[i] = new(i);
      groups[i].sample(i);
    end
    result = cg::get_coverage(covered, total);
    $display("COVERAGE %.0f %0d %0d", result, covered, total);
    $finish;
  end
endmodule

// CHECK: COVERAGE 100 16 16
// INSPECT: codec-version: 2
// INSPECT: functional-configurations: 16
// INSPECT: resolved-functional-bins: 16
