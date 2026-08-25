// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ps

module specparam_delay_cell(
    input logic source,
    input logic enable,
    output wire scalar_delayed,
    output wire tuple_delayed
);
  specparam scalar_delay = 0.1;
  specify
    specparam rise_delay = 0.2;
    specparam fall_delay = 0.3;
    specparam turnoff_delay = 0.4;
  endspecify

  assign #scalar_delay scalar_delayed = source;
  assign #(rise_delay + 0.1, fall_delay + 0.1,
           turnoff_delay + 0.1) tuple_delayed = enable ? source : 1'bz;
endmodule

module continuous_assign_specparam_delay;
  logic source = 0;
  logic enable = 1;
  wire scalar_delayed;
  wire tuple_delayed;

  specparam_delay_cell dut(source, enable, scalar_delayed, tuple_delayed);

  initial begin
    #1;
    if (scalar_delayed !== 0 || tuple_delayed !== 0)
      $fatal(1, "initial specparam-delayed values did not settle");

    source = 1;
    #0.099 if (scalar_delayed !== 0 || tuple_delayed !== 0)
      $fatal(1, "specparam rise fired early");
    #0.001 if (scalar_delayed !== 1 || tuple_delayed !== 0)
      $fatal(1, "scalar real specparam delay mismatch");
    #0.199 if (tuple_delayed !== 0)
      $fatal(1, "tuple rise expression fired early");
    #0.001 if (tuple_delayed !== 1)
      $fatal(1, "tuple rise expression delay mismatch");

    source = 0;
    #0.099 if (scalar_delayed !== 1 || tuple_delayed !== 1)
      $fatal(1, "specparam fall fired early");
    #0.001 if (scalar_delayed !== 0 || tuple_delayed !== 1)
      $fatal(1, "scalar fall delay mismatch");
    #0.299 if (tuple_delayed !== 1)
      $fatal(1, "tuple fall expression fired early");
    #0.001 if (tuple_delayed !== 0)
      $fatal(1, "tuple fall expression delay mismatch");

    enable = 0;
    #0.499 if (tuple_delayed !== 0)
      $fatal(1, "tuple turnoff expression fired early");
    #0.001 if (tuple_delayed !== 1'bz)
      $fatal(1, "tuple turnoff expression delay mismatch");

    $display("CONTINUOUS ASSIGN SPECPARAM DELAY PASS");
  end
endmodule

// CHECK: CONTINUOUS ASSIGN SPECPARAM DELAY PASS
