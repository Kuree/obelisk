// RUN: obelisk -fno-lto --std=1800-2023 -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  class node;
    int value;
    node next;

    function new(int value);
      this.value = value;
    endfunction
  endclass

  node item;
  node other;
  std::weak_reference #(node) weak_ref;

  initial begin
    item = new(9);
    other = new(2);
    item.next = other;
    other.next = item;
    weak_ref = new(item);
    $display("%0d", weak_ref.get().value);
    item = null;
    other = null;
    repeat (1100000)
      item = new(1);
    $display("%0d", weak_ref.get() == null);
    weak_ref.clear();
    $display("%0d", weak_ref.get() == null);
  end
endmodule

// CHECK: 9
// CHECK-NEXT: 1
// CHECK-NEXT: 1
