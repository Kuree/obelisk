// RUN: obelisk -fno-lto -O0 %s -o %t.native-o0
// RUN: %t.native-o0 | FileCheck %s
// RUN: obelisk -O3 %s -o %t.native-o3
// RUN: %t.native-o3 | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: %t.bytecode-o0 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.bytecode-o3 | FileCheck %s

`timescale 1ns / 1ns

module strength_cell(input wire data, input wire enable,
                     output wire destination);
  bufif1 (strong1, pull0) gate(destination, data, enable);
  specify
    (data, enable *> destination) =
        (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
  endspecify
endmodule

module specify_strength_pair_path_runtime;
  logic data = 0;
  logic enable = 1;
  wire destination;
  strength_cell dut(data, enable, destination);

  initial begin
    #5;
    if (destination !== 1'bz)
      $fatal(1, "z0 matured early");
    #1;
    if (destination !== 0)
      $fatal(1, "initial 0 did not settle");
    data = 1;
    #1;
    if (destination !== 1)
      $fatal(1, "01 did not mature");
    enable = 0;
    #4;
    if (destination !== 1)
      $fatal(1, "1z matured early");
    #1;
    if (destination !== 1'bz)
      $fatal(1, "1z did not mature");
    enable = 1;
    #3;
    if (destination !== 1'bz)
      $fatal(1, "z1 matured early");
    #1;
    if (destination !== 1)
      $fatal(1, "z1 did not mature");
    data = 0;
    #1;
    data = 1;
    #2;
    if (destination !== 1)
      $fatal(1, "cancelled 10 transition matured");
    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
