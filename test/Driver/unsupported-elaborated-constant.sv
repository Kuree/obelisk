// RUN: not obelisk -fno-lto -O0 %s -o %t 2>&1 | FileCheck %s

module unsupported_elaborated_constant;
  localparam int VALUES [0:1] = '{1, 2};

  initial $display("%0d", VALUES[0]);
endmodule

// Unpacked constant aggregates do not yet have a runtime value
// representation. Reject them at the typed preparation boundary instead of
// crashing or silently flattening away their indexing semantics.
// CHECK: error: elaborated constant has unsupported normalized type '!obelisk_sim.unpacked_array<0 : 1 x i32>'
