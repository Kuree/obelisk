// RUN: obelisk -O0 --std=1800-2023 %s -o %t.o0.native
// RUN: %t.o0.native --seed=71 > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode --std=1800-2023 %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode --seed=71 > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 --std=1800-2023 %s -o %t.o3.native
// RUN: %t.o3.native --seed=71 > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode --std=1800-2023 %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode --seed=71 > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module native_random_fork;
  initial begin
    $display("parent-before %0d", $urandom());
    fork
      begin $display("child-a %0d", $urandom()); end
      begin $display("child-b %0d", $urandom()); end
      begin
        $display("child-c-before %0d", $urandom());
        fork
          begin $display("grandchild %0d", $urandom()); end
        join
        $display("child-c-after %0d", $urandom());
      end
    join
    $display("parent-after %0d", $urandom());
  end
endmodule

// CHECK: parent-before
// CHECK: child-a
// CHECK: child-b
// CHECK: child-c-before
// CHECK: grandchild
// CHECK: child-c-after
// CHECK: parent-after
