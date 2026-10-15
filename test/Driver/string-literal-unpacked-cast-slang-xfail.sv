// XFAIL: *
// RUN: %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 5.9: a string literal cast to an unpacked byte array follows
// the assignment rules, so a size mismatch is left justified. Slang v11
// accepts the assignment but rejects the cast.
module top;
  typedef byte bytes_t[1];
  bytes_t value = bytes_t'("AB");
endmodule
