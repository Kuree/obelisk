// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 informative Annex D compatibility: count scalar net drivers,
// exclude Z contributions, retain the underlying counts during a force, and
// treat the complementary banks of one conditional primitive as one driver.
module countdrivers_pad(inout wire pad, input logic data, input logic enable);
  bufif1 b0(pad, data, enable);
endmodule

module countdrivers;
  wire scalar;
  wire [1:0] bus;
  logic data0, data1, enable0, enable1;
  integer forced, total, zeros, ones, unknowns, multiple;

  assign scalar = 1'b0;
  assign scalar = 1'b1;
  assign scalar = 1'bx;
  assign scalar = 1'bz;
  assign bus[1] = 1'b1;
  countdrivers_pad p0(bus[0], data0, enable0);
  countdrivers_pad p1(bus[0], data1, enable1);

  initial begin
    data0 = 0;
    data1 = 1;
    enable0 = 0;
    enable1 = 0;
    #1;

    multiple = $countdrivers(scalar, forced, total, zeros, ones, unknowns);
    if (multiple != 1 || forced != 0 || total != 3 || zeros != 1 ||
        ones != 1 || unknowns != 1)
      $fatal(0, "scalar counts: %0d %0d %0d %0d %0d %0d", multiple,
             forced, total, zeros, ones, unknowns);

    multiple = $countdrivers(bus[1], forced, total, zeros, ones, unknowns);
    if (multiple != 0 || forced != 0 || total != 1 || zeros != 0 ||
        ones != 1 || unknowns != 0)
      $fatal(0, "bit-select counts: %0d %0d %0d %0d %0d %0d", multiple,
             forced, total, zeros, ones, unknowns);

    multiple = $countdrivers(bus[0], forced, total, zeros, ones, unknowns);
    if (multiple != 0 || total != 0)
      $fatal(0, "disabled counts: %0d %0d", multiple, total);

    enable0 = 1;
    #1;
    multiple = $countdrivers(bus[0], forced, total, zeros, ones, unknowns);
    if (multiple != 0 || total != 1 || zeros != 1 || ones != 0 ||
        unknowns != 0)
      $fatal(0, "one enabled: %0d %0d %0d %0d %0d", multiple, total,
             zeros, ones, unknowns);

    enable1 = 1;
    #1;
    multiple = $countdrivers(bus[0], forced, total, zeros, ones, unknowns);
    if (multiple != 1 || forced != 0 || total != 2 || zeros != 1 ||
        ones != 1 || unknowns != 0)
      $fatal(0, "two enabled: %0d %0d %0d %0d %0d %0d", multiple,
             forced, total, zeros, ones, unknowns);

    force bus[0] = 1'bx;
    multiple = $countdrivers(bus[0], forced, total, zeros, ones, unknowns);
    if (multiple != 1 || forced != 1 || total != 2 || zeros != 1 ||
        ones != 1 || unknowns != 0)
      $fatal(0, "forced counts: %0d %0d %0d %0d %0d %0d", multiple,
             forced, total, zeros, ones, unknowns);
    release bus[0];

    $display("COUNTDRIVERS PASS");
  end
endmodule

// CHECK: COUNTDRIVERS PASS
