// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

module specify_edge_procedural_output_invalid(
    input wire clock, input wire data, output logic destination);
  always_ff @(posedge clock)
    destination <= data;
  specify
    (posedge clock => (destination +: data)) = 2;
  endspecify
endmodule

// Direct procedural output writes need path qualification at the write site,
// not a continuous-driver wrapper. Keep that common residual explicit until
// the procedural mapping is implemented.
// CHECK: error: edge-sensitive specify path output has no executable continuous driver; direct procedural destinations are not supported yet
