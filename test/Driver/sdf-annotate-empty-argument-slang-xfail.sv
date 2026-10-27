// XFAIL: *
// RUN: %split-file %s %t
// RUN: cd %t && %talos -emit-talos top.sv -o /dev/null

// IEEE 1800-2023 32.9 Syntax 32-1: an optional $sdf_annotate argument may be
// left empty, as `[ , [ "config_file" ] ...]` allows a comma without it.
// Slang v11 rejects every empty $sdf_annotate argument.

//--- top.sv
`timescale 1ns / 1ps
module top(input wire a, output wire y);
  assign y = a;
  specify
    (a => y) = 1;
  endspecify
  initial $sdf_annotate("delay.sdf", top, );
endmodule

//--- delay.sdf
(DELAYFILE (SDFVERSION "4.0") (TIMESCALE 1ns)
  (CELL (CELLTYPE "top") (INSTANCE) (DELAY (ABSOLUTE (IOPATH a y (3))))))
