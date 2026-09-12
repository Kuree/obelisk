// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=transition-array-consecutive
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=transition-array-consecutive
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

// IEEE 1800-2023 19.5.2 permits the multiple-bins construct for bounded
// transitions. Each concrete value and finite consecutive repetition count
// becomes a distinct resolved bin.
module top;
  bit [3:0] sampled;

  covergroup cg(input int lower, upper);
    cp: coverpoint sampled {
      bins paths[] = (1, 2 [* lower:upper] => 4);
      bins solo[] = (3 [* lower:upper]);
    }
  endgroup

  cg cov;
  task automatic take(input bit [3:0] value);
    sampled = value;
    cov.sample();
  endtask

  initial begin
    cov = new(2, 3);
    take(1); take(1); take(4); take(0);
    take(2); take(2); take(4); take(0);
    take(1); take(1); take(1); take(4); take(0);
    take(2); take(2); take(2); take(4); take(0);
    take(3); take(3); take(0);
    take(3); take(3); take(3);
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (6/6)
// CHECK: coverpoint cp: 6/6 (100.00%)
// CHECK-NEXT: bin paths[1[*2]=>4]: 2 [covered]
// CHECK-NEXT: bin paths[2[*2]=>4]: 2 [covered]
// CHECK-NEXT: bin paths[1[*3]=>4]: 1 [covered]
// CHECK-NEXT: bin paths[2[*3]=>4]: 1 [covered]
// CHECK-NEXT: bin solo[3[*2]]: 3 [covered]
// CHECK-NEXT: bin solo[3[*3]]: 1 [covered]
