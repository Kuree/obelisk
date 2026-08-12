// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/computed.sv | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-sim %t/computed.sv | FileCheck %s --check-prefix=SIM
// RUN: obelisk -emit-slang %t/literal.sv | FileCheck %s --check-prefix=LITERAL

// A part-select bound and a replication count must be constant, and
// elaboration folds them. Carrying that folded value across means arithmetic
// over parameters reaches lowering as the constant it is, rather than being
// rejected as a dynamic selection.

//--- computed.sv
module top #(parameter int W = 8) (
    output logic [7:0] narrow,
    output logic [7:0] filled,
    input logic [15:0] wide);
  localparam int L = W;
  assign narrow = wide[L-1:0];
  assign filled = {{W-1{1'b0}}, 1'b1};
endmodule

//--- literal.sv
// Nothing here is computed, so nothing needs a folded value: the literals
// already carry their own.
module top (output logic [7:0] narrow, input logic [15:0] wide);
  assign narrow = wide[7:0];
endmodule

// The computed bound carries the value elaboration folded for it.
// SLANG: slang.expression.binary_op
// SLANG-SAME: folded_constant = "7"

// It reaches lowering as a static extract, exactly as wide[7:0] would.
// SIM: obelisk_sim.logic.extract %{{.*}} from 0 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<8>
// SIM-NOT: dynamic_extract

// LITERAL-NOT: folded_constant
