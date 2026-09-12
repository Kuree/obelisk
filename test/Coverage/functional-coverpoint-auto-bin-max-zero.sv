// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   -o %t.native %s
// RUN: %t.native > %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   -o %t.bytecode %s
// RUN: %t.bytecode > %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit value;

  covergroup cg;
    cp: coverpoint value {
      option.auto_bin_max = 0;
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    $display("zero %.6f", cov.get_inst_coverage());
    $finish;
  end
endmodule

// IEEE 1800-2017 19.11 excludes an empty point from its parent computation;
// the nonzero-weight covergroup therefore has a zero denominator and returns
// 0.0. The resolved v1 schema retains an explicit zero-cardinality auto group.
// CHECK: zero 0.000000
