// XFAIL: *
// RUN: %talos -emit-slang %s -o /dev/null

// IEEE 1800-2023 11.4.14.3: a streaming concatenation target unpacks into the
// data members of an existing object. Slang v11 rejects a class handle as a
// stream expression of an unpack.
class C;
  bit data;
endclass
module top;
  C c;
  initial {>>{c}} = 1'b1;
endmodule
