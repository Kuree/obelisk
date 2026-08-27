// XFAIL: *
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

// Slang includes static class properties when checking the bit-stream width,
// although only instance properties belong to the object bit stream.
class C;
  static byte ignored;
  byte payload;
endclass

module class_bitstream_static_property_xfail;
  C c;
  bit [7:0] bits;
  initial begin
    c = new;
    bits = bit [7:0]'(c);
  end
endmodule
