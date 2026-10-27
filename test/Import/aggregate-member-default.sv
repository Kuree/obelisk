// RUN: talos -emit-talos %s | FileCheck %s --check-prefix=SEMANTIC

// Verify imported aggregate member defaults without invoking lowering.

module aggregate_member_default;
  parameter bit [3:0] DEFAULT_LO = 4'h5;
  struct {
    bit [3:0] lo = DEFAULT_LO;
    bit [3:0] hi;
  } value;
endmodule

// SEMANTIC: talos.sv.symbol.variable
// SEMANTIC-SAME: talos.aggregate_member_initializer_ordinals
// SEMANTIC: talos.sv.expression.named_value
