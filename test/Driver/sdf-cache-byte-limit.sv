// RUN: %split-file %s %t
// RUN: cd %t && not %test-talos --test-max-sdf-cache-bytes=64 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// Failed files remain charged to the deterministic Clause 32 static-input
// budget, so distinct malformed files cannot evade the aggregate byte limit.
// Two 32-byte files fill the lowered 64-byte budget.
// CHECK-COUNT-1: error: static SDF annotation aggregate byte resource limit exceeded

//--- design.sv
`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_aggregate_byte_resource;
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
    $sdf_annotate("bytes-0.sdf", dut);
    $sdf_annotate("bytes-1.sdf", dut);
    $sdf_annotate("bytes-2.sdf", dut);
  end
endmodule
//--- bytes-0.sdf
)xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
//--- bytes-1.sdf
)xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
//--- bytes-2.sdf
)
