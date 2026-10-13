// RUN: %split-file %s %t
// RUN: cd %t && not %test-obelisk --test-max-sdf-application-work=8 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// Production application (not parser coverage): every annotation matches only
// the final specify path. Bound the Clause 32.4 candidate cross product even
// though neither the parsed-entry nor successful-update limit is approached.
// Four annotations probe four paths each against a lowered limit of eight.
// CHECK-COUNT-1: error: static SDF annotation application-work resource limit exceeded
// CHECK-NOT: warning:

//--- design.sv
`timescale 1fs / 1fs
module sdf_match_attempt_leaf(
    input wire [3:0] source,
    output wire [3:0] destination);
  assign destination = source;
  specify
    (source[0] => destination[0]) = 1;
    (source[1] => destination[1]) = 1;
    (source[2] => destination[2]) = 1;
    (source[3] => destination[3]) = 1;
  endspecify
endmodule
module sdf_match_attempt_resource;
  logic [3:0] source;
  wire [3:0] destination;
  sdf_match_attempt_leaf dut(source, destination);
  initial $sdf_annotate("attempts.sdf");
endmodule
//--- attempts.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL
    (CELLTYPE "sdf_match_attempt_leaf")
    (INSTANCE dut)
    (DELAY (ABSOLUTE
      (IOPATH source[3] destination[3] (1))
      (IOPATH source[3] destination[3] (1))
      (IOPATH source[3] destination[3] (1))
      (IOPATH source[3] destination[3] (1))
    ))))
