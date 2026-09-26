// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

module continuous_assign_time_zero_delay;
  logic source;
  wire delayed;
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
// RUN: obelisk -O0 --vpi=off --coverage=line %s -o %t.coverage.native
// RUN: %t.coverage.native --coverage-output=%t.coverage.native.obcov
// RUN: obelisk-cov report --format=lcov %t.coverage.native.obcov | FileCheck %s --check-prefix=COVERAGE
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode --coverage=line %s -o %t.coverage.bytecode
// RUN: %t.coverage.bytecode --coverage-output=%t.coverage.bytecode.obcov
// RUN: obelisk-cov report --format=lcov %t.coverage.bytecode.obcov | FileCheck %s --check-prefix=COVERAGE
// COVERAGE: DA:15,3
