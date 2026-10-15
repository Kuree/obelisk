// XFAIL: *
// RUN: not %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 7.3: dynamic types such as string may appear only in tagged
// unions. Slang v11 accepts a string member of an untagged union.
module top;
  typedef union { string s; } U;
endmodule
