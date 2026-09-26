// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module native_trireg_charge;
  logic enable;
  logic source;

  // IEEE 1800-2017 6.6.4 and 28.16.2: all three legal charge strengths
  // retain the last driven value while the driver is z, then the third delay
  // changes the stored charge to x.
  trireg (small) #(0, 0, 3) small_cap;
  trireg (medium) #(0, 0, 3) medium_cap;
  trireg (large) #(0, 0, 3) large_cap;
  assign small_cap = enable ? source : 1'bz;
  assign medium_cap = enable ? source : 1'bz;
  assign large_cap = enable ? source : 1'bz;

  initial begin
    enable = 1'b1;
    source = 1'b1;
    #1;
    assert (small_cap === 1'b1);
    assert (medium_cap === 1'b1);
    assert (large_cap === 1'b1);

    enable = 1'b0;
    #2;
    assert (small_cap === 1'b1);
    assert (medium_cap === 1'b1);
    assert (large_cap === 1'b1);
    #2;
    assert (small_cap === 1'bx);
    assert (medium_cap === 1'bx);
    assert (large_cap === 1'bx);
    $display("trireg charge passed");
  end
endmodule

// CHECK: trireg charge passed
