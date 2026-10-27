// XFAIL: *
// RUN: not %talos -emit-slang %s -o /dev/null

// IEEE 1800-2023 8.19: an instance constant can be assigned only once in
// its class constructor. Slang v11 rejects an assignment outside the
// constructor but accepts a second one inside it.
class C;
  const int value;
  function new();
    value = 1;
    value = 2;
  endfunction
endclass
module top;
endmodule
