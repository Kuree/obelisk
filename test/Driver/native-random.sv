// RUN: obelisk -O0 --std=1800-2017 %s -o %t.17.o0.native
// RUN: %t.17.o0.native --seed=5 > %t.17.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode --std=1800-2017 %s -o %t.17.o0.bytecode
// RUN: %t.17.o0.bytecode --seed=5 > %t.17.o0.bytecode.out
// RUN: diff -u %t.17.o0.native.out %t.17.o0.bytecode.out
// RUN: obelisk -O3 --std=1800-2017 %s -o %t.17.o3.native
// RUN: %t.17.o3.native --seed=5 > %t.17.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode --std=1800-2017 %s -o %t.17.o3.bytecode
// RUN: %t.17.o3.bytecode --seed=5 > %t.17.o3.bytecode.out
// RUN: diff -u %t.17.o0.native.out %t.17.o3.native.out
// RUN: diff -u %t.17.o0.native.out %t.17.o3.bytecode.out
// RUN: obelisk -O0 --std=1800-2023 %s -o %t.23.o0.native
// RUN: %t.23.o0.native --seed=5 > %t.23.o0.native.out
// RUN: diff -u %t.17.o0.native.out %t.23.o0.native.out
// RUN: %t.23.o0.native --seed=6 > %t.seed6.out
// RUN: not /usr/bin/cmp -s %t.23.o0.native.out %t.seed6.out
// RUN: FileCheck %s < %t.23.o0.native.out

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

// CHECK: 2432729949
// CHECK-NEXT: 8
// CHECK-NEXT: 457780463
