// RUN: cd %S && obelisk -emit-slang %s -o %t.slang.mlir
// RUN: FileCheck %s --check-prefix=MLIR < %t.slang.mlir
// RUN: FileCheck %s --check-prefix=NO-SDF < %t.slang.mlir
// RUN: cd %S && obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.aot.ll
// RUN: FileCheck %s --check-prefix=AOT-NO-SDF < %t.aot.ll
// RUN: cd %S && obelisk -fno-lto -O0 --native-scheduler=generic %s -o %t.generic-o0
// RUN: cd %S && obelisk -fno-lto -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: cd %S && obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: cd %S && obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: cd %S && obelisk -fno-lto -O0 --native-scheduler=auto %s -o %t.auto-o0
// RUN: cd %S && obelisk -fno-lto -O3 --native-scheduler=auto %s -o %t.auto-o3
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.generic-o3 > %t.generic-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: %t.auto-o0 > %t.auto-o0.out
// RUN: %t.auto-o3 > %t.auto-o3.out
// RUN: diff -u %t.generic-o0.out %t.generic-o3.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o0.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o3.out
// RUN: diff -u %t.generic-o0.out %t.auto-o0.out
// RUN: diff -u %t.generic-o0.out %t.auto-o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic-o0.out

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
// NO-SDF-NOT: obelisk.sdf.table
// NO-SDF-NOT: obelisk_sdf.
// AOT-NO-SDF-NOT: @__obelisk_sdf
// AOT-NO-SDF-NOT: @obelisk_rt_v1_sdf
// AOT-NO-SDF-NOT: sdf_annotate
// AOT-NO-SDF-NOT: obelisk_sdf.
// OUTPUT: SDF INCREMENT PASSED
