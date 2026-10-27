// RUN: cd %S && talos -emit-slang %s -o - | FileCheck %s --check-prefix=MLIR --check-prefix=NO-SDF
// RUN: cd %S && talos -O3 --native-scheduler=auto -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT-NO-SDF
// RUN: cd %S && talos -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic > %t.generic.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic.out
// RUN: cd %S && talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.generic.out -

`timescale 1ns / 1ns

module sdf_increment_cell(input wire source, output wire destination);
  assign destination = source;
  specify
    (source => destination) = (9, 9);
  endspecify
endmodule

module sdf_increment_multiple;
  logic source;
  wire destination;
  sdf_increment_cell dut(source, destination);

  initial begin
    // IEEE 1800-2017 32.5 and 32.6: calls in one startup sequence apply in
    // statement order; sparse ABSOLUTE fields retain the current value before
    // the following signed INCREMENT is added. Exact half-quantum rounding
    // maps +2.5 ns to +3 ns and -1.5 ns to -2 ns before that addition.
    $sdf_annotate("Inputs/sdf-multiple-absolute.sdf", dut);
    $sdf_annotate("Inputs/sdf-increment.sdf", dut);

    source = 0;
    #9;
    if (destination !== 0)
      $fatal(1, "initial path transition did not settle");
    source = 1;
    #7;
    if (destination !== 0)
      $fatal(1, "incremented rise happened too early");
    #1;
    if (destination !== 1)
      $fatal(1, "incremented rise did not happen");
    source = 0;
    #6;
    if (destination !== 1)
      $fatal(1, "decremented fall happened too early");
    #1;
    if (destination !== 0)
      $fatal(1, "decremented fall did not happen");
    $display("SDF INCREMENT PASSED");
    $finish;
  end
endmodule

// MLIR: timing_delay_count = 12 : i64
// MLIR: timing_delay_fs = array<i64: 8000000, 7000000, 8000000, 8000000, 7000000, 7000000, 8000000, 8000000, 7000000, 7000000, 12000000, 7000000>
// NO-SDF-NOT: slang.symbol.sdf
// NO-SDF-NOT: talos.sdf.table
// NO-SDF-NOT: talos_sdf.
// AOT-NO-SDF-NOT: @__talos_sdf
// AOT-NO-SDF-NOT: @talos_rt_v1_sdf
// AOT-NO-SDF-NOT: sdf_annotate
// AOT-NO-SDF-NOT: talos_sdf.
// OUTPUT: SDF INCREMENT PASSED
