// XFAIL: *
// RUN: %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 21.3.4.4: $fread reads an unpacked struct member by member
// in declaration order, and an unpacked union through its first member.
// Slang v11 admits only integral and unpacked-array destinations.
module top;
  typedef struct { byte a; } s_t;
  s_t value;
  int fd, n;
  initial n = $fread(value, fd);
endmodule
