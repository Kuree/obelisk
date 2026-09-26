// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s

`timescale 1ns / 1ns

module primitive_parameter_delay;
  localparam int BASE = 4;
  localparam int RISE = BASE;
  localparam int FALL = BASE - 1;
  localparam int TURNOFF = FALL - 1;

  logic data;
  wire result;
  nmos #(RISE, FALL, TURNOFF) n0(result, data, 1'b1);

  initial begin
    data = 1'b1;
    #3 if (result === 1'b1) $fatal(0, "rise delay fired early");
    #1 if (result !== 1'b1) $fatal(0, "rise delay mismatch");

    data = 1'b0;
    #2 if (result === 1'b0) $fatal(0, "fall delay fired early");
    #1 if (result !== 1'b0) $fatal(0, "fall delay mismatch");

    data = 1'bz;
    #1 if (result === 1'bz) $fatal(0, "turnoff delay fired early");
    #1 if (result !== 1'bz) $fatal(0, "turnoff delay mismatch");
    $display("PRIMITIVE PARAMETER DELAY PASS");
  end
endmodule

// CHECK: PRIMITIVE PARAMETER DELAY PASS
