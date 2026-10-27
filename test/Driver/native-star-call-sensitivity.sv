// RUN: talos %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: talos --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

`timescale 1ns/1ps
module native_star_call_sensitivity;
  logic source;
  logic hidden;
  logic write_only;
  logic result;

  function logic sample(logic value);
    write_only = 1;
    sample = value | hidden;
  endfunction

  initial begin
    source = 0;
    hidden = 0;
    write_only = 0;
    // IEEE 1800-2023 9.2.2.2.2: @* watches a call's arguments, not what the
    // function body reads or writes.
    #1 hidden = 1;
    #1 write_only = 1;
    #1 source = 1;
  end

  initial begin
    @*
      result = sample(source);
    $display("star-call=%0d at %0t", result, $time);
  end
endmodule

// CHECK: star-call=1 at 3
