// RUN: not obelisk -emit-slang %s -o /dev/null 2>&1 | FileCheck %s

module sdf_nonconstant_filename;
  string filename = "runtime-selected.sdf";
  initial $sdf_annotate(filename);
endmodule

// CHECK: sdf-nonconstant-filename.sv:5:11: error: $sdf_annotate requires a constant string filename and an elaborated module scope
