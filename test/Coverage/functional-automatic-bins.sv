// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=automatic
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=automatic
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [2:0] small_value;
  logic [2:0] logic_value;
  bit [7:0] wide;
  bit signed [7:0] signed_value;
  bit [2:0] ignored_value;

  covergroup cg;
    small_cp: coverpoint small_value;
    logic_cp: coverpoint logic_value;
    wide_cp: coverpoint wide;
    signed_cp: coverpoint signed_value;
    ignored_cp: coverpoint ignored_value {
      ignore_bins ignored = {[0:1], [5:6]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;

    small_value = 0;
    logic_value = 0;
    wide = 0;
    signed_value = -128;
    ignored_value = 2;
    cov.sample();

    small_value = 7;
    logic_value = 'x;
    wide = 4;
    signed_value = 127;
    ignored_value = 4;
    cov.sample();

    wide = 255;
    logic_value = 7;
    ignored_value = 7;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 26.56% (12/148)
// CHECK-DAG: coverpoint small_cp: 2/8 (25.00%)
// CHECK-DAG: coverpoint logic_cp: 2/8 (25.00%)
// CHECK-DAG: coverpoint wide_cp: 3/64 (4.69%)
// CHECK-DAG: coverpoint signed_cp: 2/64 (3.12%)
// CHECK-DAG: coverpoint ignored_cp: 3/4 (75.00%)
// CHECK-DAG: bin auto[0]: 1 [covered]
// CHECK-DAG: bin auto[7]: {{[1-9][0-9]*}} [covered]
// CHECK-DAG: bin auto[0:3]: 1 [covered]
// CHECK-DAG: bin auto[4:7]: 1 [covered]
// CHECK-DAG: bin auto[252:255]: 1 [covered]
// CHECK-DAG: bin auto[-128:-125]: 1 [covered]
// CHECK-DAG: bin auto[124:127]: {{[1-9][0-9]*}} [covered]
// CHECK-DAG: bin ignored: 0 [excluded]
