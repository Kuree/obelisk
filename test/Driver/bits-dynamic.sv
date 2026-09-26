// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// IEEE 1800-2017 20.6.2 and 9.4.2: $bits returns the live total bit count for
// a dynamically sized bitstream, and a size change is a readable dependency
// of an implicit event-control expression.
module bits_dynamic;
  byte stream[$];
  logic [4:0] words[];
  string text;
  int observed;

  always_comb observed = $bits(stream);

  initial begin
    stream = '{8'h11, 8'h22};
    words = new[3];
    text = "abc";
    #0;
    $display("initial=%0d:%0d:%0d:%0d", $bits(stream), $bits(words),
             $bits(text), observed);

    stream.push_back(8'h33);
    words = new[1](words);
    text = "hello";
    #0;
    $display("changed=%0d:%0d:%0d:%0d", $bits(stream), $bits(words),
             $bits(text), observed);

    stream.delete();
    words.delete();
    text = "";
    #0;
    $display("empty=%0d:%0d:%0d:%0d", $bits(stream), $bits(words),
             $bits(text), observed);
  end
endmodule

// CHECK: initial=16:15:24:16
// CHECK-NEXT: changed=24:5:40:24
// CHECK-NEXT: empty=0:0:0:0
