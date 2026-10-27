// XFAIL: *
// RUN: %talos -emit-slang %s -o /dev/null

// IEEE 1800-2023 7.9.8 allows a traversal argument narrower than the index
// type: the method truncates the index and returns -1. Slang v11 binds the
// argument as an ordinary ref and rejects the byte as not equivalent to int.
module top;
  bit a[int];
  byte i;
  initial void'(a.first(i));
endmodule
