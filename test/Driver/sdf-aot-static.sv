// RUN: cd %S && obelisk -fno-lto -O3 --native-scheduler=aot %s -o %t.aot-o3
// RUN: %t.aot-o3 | FileCheck %s

`timescale 1ns / 1ns

module sdf_aot_static;
  logic value = 0;

  initial begin
    // The matching CELL has no IOPATH entries. This isolates the statically
    // consumed Clause 32.9 task itself and guards AOT eligibility against a
    // future runtime SDF reader or hierarchical lookup.
    $sdf_annotate("Inputs/sdf-aot-static.sdf");
    #1 value = 1;
    #1 $display("SDF AOT %0b", value);
  end
endmodule

// CHECK: SDF AOT 1
