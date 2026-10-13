// RUN: obelisk -O3 --vpi=off %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

`timescale 1ns / 1ns

module continuous_assign_time_zero_delay;
  logic source;
  wire delayed;
  // COVERAGE: DA:[[# @LINE + 1]],3
  assign #10 delayed = source;

  initial begin
    source = 1'b0;
    #9 if (delayed !== 1'bz)
      $fatal(0, "time-zero drive fired early");
    #1 if (delayed !== 1'b0)
      $fatal(0, "time-zero drive was omitted");

    source = 1'b1;
    #9 if (delayed !== 1'b0)
      $fatal(0, "later drive fired early");
    #1 if (delayed !== 1'b1)
      $fatal(0, "later drive was omitted");
    $display("CONTINUOUS ASSIGN DELAY PASS");
  end
endmodule

// CHECK: CONTINUOUS ASSIGN DELAY PASS

// Coverage must observe each semantic evaluation: the initial X evaluation,
// the time-zero initialization assignment, and the later source transition.
