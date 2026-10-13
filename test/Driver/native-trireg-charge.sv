// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

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
