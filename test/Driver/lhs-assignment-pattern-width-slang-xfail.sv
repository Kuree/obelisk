// XFAIL: *
// RUN: not %talos -emit-slang %s -o /dev/null

// IEEE 1800-2023 10.9: each member of a left-hand assignment pattern has the
// same number of bits as its right-hand element. Slang v11 accepts a one-bit
// member for a 32-bit element.
module top;
  function void f();
    int src[1];
    bit dst;
    '{dst} = src;
  endfunction
endmodule
