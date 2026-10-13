// RUN: obelisk -O3 --top=top %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --top=top --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

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
