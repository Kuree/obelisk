// XFAIL: *
// RUN: not %talos -emit-slang %s -o /dev/null

// IEEE 1800-2023 15.4.9: the compiler verifies that put, try_put, peek,
// try_peek, get, and try_get of a parameterized mailbox use argument types
// equivalent to the mailbox type. Slang v11 accepts an assignment-compatible
// byte message for a mailbox of int.
module top;
  mailbox #(int) m;
  initial m.put(byte'(0));
endmodule
