// XFAIL: *
// RUN: not %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 5.7.1: a decimal literal may hold an x, z or ? digit only as
// its single digit. Slang v11 accepts 1x, which mixes a digit with x.
module top;
  parameter V = 2'd1x;
endmodule
