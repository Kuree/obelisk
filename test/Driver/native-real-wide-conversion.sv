// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

// LRM 6.12, 6.12.1: integer conversion uses IEEE 754 rounding and overflow
// independently of the target's arbitrary-width integer-to-float legalization.
module native_real_wide_conversion;
  bit [1087:0] wide;
  bit signed [1087:0] signed_wide;
  bit [64:0] halfway;
  bit [64:0] roundtrip;
  real converted;
  shortreal short_converted;

  initial begin
    wide = 0;
    wide[1087] = 1;
    converted = wide;
    $display("positive-overflow=%0d", converted > 1.0e308);
    signed_wide = wide;
    converted = signed_wide;
    $display("negative-overflow=%0d", converted < -1.0e308);
    wide = 0;
    wide[128] = 1;
    short_converted = wide;
    $display("short-overflow=%0d", short_converted > 3.4e38);
    wide = 0;
    wide[70] = 1;
    converted = wide;
    $display("finite=%0d", converted == 1180591620717411303424.0);
    halfway = 65'd18446744073709553664;
    converted = halfway;
    roundtrip = converted;
    $display("tie-even-low=%0d", roundtrip[15:0]);
    halfway = 65'd18446744073709553665;
    converted = halfway;
    roundtrip = converted;
    $display("above-tie-low=%0d", roundtrip[15:0]);
  end
endmodule

// CHECK: positive-overflow=1
// CHECK-NEXT: negative-overflow=1
// CHECK-NEXT: short-overflow=1
// CHECK-NEXT: finite=1
// CHECK-NEXT: tie-even-low=0
// CHECK-NEXT: above-tie-low=4096
