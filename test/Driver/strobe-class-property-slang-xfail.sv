// XFAIL: *
// RUN: %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 21.2.2: $strobe takes the same arguments as $display, and
// 6.21 permits references to members of dynamic variables in procedural
// blocks. Slang v11 rejects the class property as an automatic variable that
// cannot be traced.
class C;
  int value;
endclass
module top;
  C obj = new;
  initial $strobe("%0d", obj.value);
endmodule
