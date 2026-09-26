// RUN: %obelisk --std=1800-2023 -O0 --target=native -o %t.native %s
// RUN: %t.native > %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   -o %t.bytecode %s
// RUN: %t.bytecode > %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit value;

  covergroup cg(input int instance_weight);
    option.weight = instance_weight;
    cp: coverpoint value {
      option.weight = 0;
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg weighted;
  cg weightless;
  initial begin
    weighted = new(1);
    weightless = new(0);
    $display("weighted %.6f", weighted.get_inst_coverage());
    $display("weightless %.6f", weightless.get_inst_coverage());
    $finish;
  end
endmodule

// IEEE 1800-2017 19.11: a covergroup with a zero item-weight denominator
// returns 0 for nonzero group weight and 100 for zero group weight.
// CHECK: weighted 0.000000
// CHECK-NEXT: weightless 100.000000
