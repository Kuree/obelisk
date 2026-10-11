// RUN: obelisk -O3 --std=1800-2017 %s -o %t.17
// RUN: %t.17 --seed=5 > %t.17.out
// RUN: obelisk -O3 --execution-tier=bytecode --std=1800-2017 %s -o %t.17.bytecode
// RUN: %t.17.bytecode --seed=5 | diff -u %t.17.out -
// RUN: obelisk -O3 --std=1800-2023 %s -o %t.23
// RUN: %t.23 --seed=5 | diff -u %t.17.out -
// RUN: %t.23 --seed=6 | not diff -q %t.17.out -
// RUN: FileCheck %s < %t.17.out

module native_random;
  int seed = 23;
  initial begin
    $display("%0d", $urandom());
    $display("%0d", $urandom_range(9, 3));
    $display("%0d", $urandom(17));
    $display("%0d", $random());
    $display("%0d", $random(seed));
    $display("%0d", seed);
  end
endmodule

// CHECK: 3141403713
// CHECK-NEXT: 8
// CHECK-NEXT: 2340066555
