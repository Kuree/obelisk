// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=enum-auto
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=enum-auto
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  typedef enum logic [3:0] {
    RED = 1,
    GREEN = 5,
    BLUE = 12,
    UNKNOWN = 'x
  } color_t;

  color_t color;

  covergroup cg;
    colors: coverpoint color;
    color_t explicit_colors: coverpoint color;
    int integral_colors: coverpoint color;
    filtered: coverpoint color {
      illegal_bins reserved = {GREEN};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    color = RED;
    cov.sample();
    color = BLUE;
    cov.sample();
    color = UNKNOWN;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 58.72% (7/72)
// CHECK-DAG: coverpoint colors: 2/3 (66.67%)
// CHECK-DAG: coverpoint explicit_colors: 2/3 (66.67%)
// CHECK-DAG: coverpoint integral_colors: 1/64 (1.56%)
// CHECK-DAG: coverpoint filtered: 2/2 (100.00%)
// CHECK-DAG: bin auto[RED]: 1 [covered]
// CHECK-DAG: bin auto[GREEN]: 0 [uncovered]
// CHECK-DAG: bin auto[BLUE]: 1 [covered]
// CHECK-DAG: bin auto[UNKNOWN]: 0 [excluded]
// CHECK-DAG: bin reserved: 0 [excluded]
// CHECK-DAG: bin auto[0:67108863]: 3 [covered]
