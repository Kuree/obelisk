// RUN: obelisk -emit-sim %s -o - | FileCheck %s
// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode

module specify_edge_procedural_wide(
    input wire clock, input wire [4095:0] data,
    output logic [4095:0] destination);
  always @(posedge clock)
    destination <= data;
  specify
    (posedge clock *> (destination +: data)) = 2;
  endspecify
endmodule

// One packed qualification state and one packed write operation: neither the
// frontend plan nor runtime actor state grows per destination bit.
// CHECK-COUNT-1: debug "__obelisk_timing_path_edge_pending"
// CHECK-COUNT-1: obelisk_sim.ref.store_inertial_path
