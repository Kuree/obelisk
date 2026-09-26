// RUN: cd %S && obelisk -O0 --native-scheduler=generic %s -o %t.generic-o0
// RUN: cd %S && obelisk -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: cd %S && not obelisk -O3 --native-scheduler=aot %s -o %t.aot-o3 2>&1 | FileCheck %s --check-prefix=AOT
// RUN: cd %S && obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: cd %S && obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.generic-o3 > %t.generic-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: diff -u %t.generic-o0.out %t.generic-o3.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o0.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic-o0.out

`timescale 1ns / 1ns

module sdf_delay_cell(input wire source, output wire destination);
  assign destination = source;
  specify
    (source => destination) = (9, 9);
  endspecify
endmodule

module sdf_iopath_absolute;
  logic source;
  wire destination;
  sdf_delay_cell dut(source, destination);

  initial begin
    // The explicit module argument makes an empty SDF INSTANCE name refer to
    // dut, as specified by IEEE 1800-2017 32.9.
    $sdf_annotate("Inputs/sdf-iopath-absolute.sdf", dut);
    source = 0;
    #4;
    if (destination !== 0)
      $fatal(1, "initial fall delay was not annotated");
    source = 1;
    #1;
    if (destination !== 0)
      $fatal(1, "annotated rise happened too early");
    #1;
    if (destination !== 1)
      $fatal(1, "annotated rise did not happen");
    source = 0;
    #2;
    if (destination !== 1)
      $fatal(1, "annotated fall happened too early");
    #1;
    if (destination !== 0)
      $fatal(1, "annotated fall did not happen");
    $display("SDF PASSED");
    $finish;
  end
endmodule

// OUTPUT: SDF PASSED
// AOT: design is ineligible for native AOT scheduling: delayed continuous assignment requires generic ordering
