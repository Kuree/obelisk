// RUN: obelisk -fno-lto --std=1800-2023 -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.native.out %t.o3.native.out
// RUN: diff -u %t.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  class node;
    int value;
    logic [7:0] flags;
    node next;
  endclass

  class pressure;
    bit [262143:0] payload;
  endclass

  node object;
  node target;
  pressure garbage;
  std::weak_reference #(node) weak_object;
  std::weak_reference #(node) weak_target;

  initial begin
    object = new;
    target = new;
    target.value = 23;
    weak_object = new(object);
    weak_target = new(target);

    object.value <= 11;
    object.value <= #2 17;
    object.flags <= #2 8'hxx;
    object.next <= #2 target;
    object = null;
    target = null;

    // The pending updates are the only strong roots during this collection.
    repeat (1100)
      garbage = new;
    #3;

    object = weak_object.get();
    target = weak_target.get();
    $display("%0d %0d %0d %0d", object != null, object.value,
             object.flags === 8'hxx, object.next == target);

    object.next <= null;
    #1;
    $display("%0d", object.next == null);

    // Committing the update must release both the destination and value pins.
    object = null;
    target = null;
    repeat (1100)
      garbage = new;
    $display("%0d %0d", weak_object.get() == null,
             weak_target.get() == null);
  end
endmodule

// CHECK: 1 17 1 1
// CHECK-NEXT: 1
// CHECK-NEXT: 1 1
