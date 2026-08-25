// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

module specify_edge_internal_delay_invalid(
    input wire clock, input wire data, output wire destination);
  logic state;
  always @(posedge clock)
    #1 state <= data;
  assign destination = state;
  specify
    (posedge clock => (destination +: data)) = 2;
  endspecify
endmodule

// Same-time qualification deliberately does not pair a path event with a
// destination change after simulation time has advanced.
// CHECK: error: edge-sensitive specify path has an internally delayed destination dependency; same-time path qualification cannot be paired safely
