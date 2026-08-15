// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

module unsupported_elaborated_constant;
  localparam int VALUES [0:1] = '{1, 2};

  initial $display("%0d", VALUES[0]);
endmodule

// CHECK: 1
