// XFAIL: *
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

// Slang checks the nominal Base width and rejects a legal cast whose runtime
// object is Derived; object bit-stream width follows the concrete object.
class Base;
  byte base;
endclass

class Derived extends Base;
  byte derived;
endclass

module class_bitstream_nominal_width_xfail;
  Base handle;
  Derived derived;
  bit [15:0] bits;
  initial begin
    derived = new;
    handle = derived;
    bits = bit [15:0]'(handle);
  end
endmodule
