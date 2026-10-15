// XFAIL: *
// RUN: %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 11.4.14.1: a null class handle streams nothing, so a stream
// holding a class operand has no static width to check against its target.
// Slang v11 rejects the one-bit target because it counts the class members.
class C;
  bit data;
endclass
module top;
  C c;
  bit r;
  initial r = {>>{c, 1'b1}};
endmodule
