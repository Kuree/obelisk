// RUN: obelisk -fno-lto %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

`timescale 1ns/1ps
module native_star_call_sensitivity;
  logic source;
  logic write_only;
  logic result;

  function logic sample;
    write_only = 1;
    sample = source;
  endfunction

  initial begin
    source = 0;
    write_only = 0;
    #1 write_only = 1;
    #1 source = 1;
  end

  initial begin
    @*
      result = sample();
    $display("star-call=%0d", result);
  end
endmodule

// CHECK: star-call=1
