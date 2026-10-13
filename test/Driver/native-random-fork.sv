// RUN: obelisk -O3 --std=1800-2023 %s -o %t.native
// RUN: %t.native --seed=71 > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode --std=1800-2023 %s -o %t.bytecode
// RUN: %t.bytecode --seed=71 | diff -u %t.native.out -

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
