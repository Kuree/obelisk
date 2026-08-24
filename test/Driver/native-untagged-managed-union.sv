// RUN: obelisk -fno-lto -O0 --top=top %s -o %t.o0.native
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode --top=top %s -o %t.o0.bytecode
// RUN: obelisk -fno-lto -O3 --top=top %s -o %t.o3.native
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --top=top %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module top;
  class Node;
    int value;
  endclass

  typedef union {
    Node object;
    string text;
    logic [63:0] bits;
  } choice_t;

  choice_t choice;
  Node node;
  initial begin
    // IEEE 1800-2017 7.3.2: untagged union members overlap. Managed arms use
    // validated candidate roots, including when the overlapping integral arm
    // has a separate unknown plane.
    node = new;
    node.value = 42;
    choice.object = node;
    node = null;
    repeat (2000) begin
      node = new;
      node.value = 7;
    end
    assert (choice.object.value == 42);

    choice.bits = 'x;
    assert ($isunknown(choice.bits));
    choice.text = "safe";
    assert (choice.text == "safe");
    $display("managed union passed");
  end
endmodule

// CHECK: managed union passed
