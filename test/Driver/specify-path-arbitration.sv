// RUN: obelisk -emit-slang %s -o %t.slang.mlir
// RUN: FileCheck %s --check-prefix=SLANG < %t.slang.mlir

`timescale 1ns / 1ns

module arbitrated_path(input wire enable, input wire data, output wire value);
  assign value = enable ? data : 1'bz;
  specify
    (data => value) = 2;
    (enable *> value) = 4;
  endspecify
endmodule

module specify_path_arbitration;
  wire enable;
  wire data;
  wire value;
  arbitrated_path dut(enable, data, value);
endmodule

// SLANG-DAG: timing_input_path = "specify_path_arbitration.dut.data"
// SLANG-DAG: timing_input_path = "specify_path_arbitration.dut.enable"
