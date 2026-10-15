// RUN: %split-file %s %t
// RUN: cd %S/Inputs && obelisk -emit-slang %t/initializer.sv -o - | FileCheck %s --check-prefix=APPLIED
// RUN: not obelisk -emit-slang %t/written.sv -o /dev/null 2>&1 | FileCheck %s --check-prefix=WRITTEN

// IEEE 1800-2023 32.9 accepts any string or integral filename expression.
// Annotation runs while elaborating, so a filename variable is accepted when
// its startup value is known: a static variable that nothing writes keeps its
// constant initializer.
// APPLIED: delay_fs = array<i64: 2000000, 3000000>
// APPLIED: sdf_compile_time_applied

// A variable that another process may write before the annotation runs is
// still rejected.
// WRITTEN: written.sv:5:5: error: $sdf_annotate requires a constant string filename and an elaborated module scope

//--- initializer.sv
`timescale 1ns / 1ns
module sdf_delay_cell(input wire source, output wire destination);
  assign destination = source;
  specify
    (source => destination) = (9, 9);
  endspecify
endmodule
module sdf_variable_filename;
  logic source;
  wire destination;
  sdf_delay_cell dut(source, destination);
  string filename = "sdf-iopath-absolute.sdf";
  initial $sdf_annotate(filename, dut);
endmodule

//--- written.sv
module sdf_written_filename;
  string filename = "runtime-selected.sdf";
  initial filename = "other.sdf";
  initial
    $sdf_annotate(filename);
endmodule
