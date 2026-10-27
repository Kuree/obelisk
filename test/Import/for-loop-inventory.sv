// RUN: talos -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: talos -emit-talos %s | FileCheck %s --check-prefix=TALOS

module for_loop_inventory;
  integer i;
  integer j;

  initial begin
    for (i = 0, j = 1; i < 2; i++, j = j + 2)
      j = j + i;

    for (;;)
      break;

    for (int x = 0, y = 1; x < 2; x++, y--)
      i = i + x + y;
  end
endmodule

// Expression initializers are children of the for-loop node.
// SLANG: slang.statement.for_loop attributes {
// SLANG-SAME: has_condition = true
// SLANG-SAME: initializer_count = 2 : i64
// SLANG-SAME: step_count = 2 : i64
// SLANG: slang.statement.for_loop attributes {
// SLANG-SAME: has_condition = false
// SLANG-SAME: initializer_count = 0 : i64
// SLANG-SAME: step_count = 0 : i64
// Loop-variable declarations are explicit preceding statements; only the
// condition and steps belong to this node's expression inventory.
// SLANG: slang.statement.for_loop attributes {
// SLANG-SAME: has_condition = true
// SLANG-SAME: initializer_count = 0 : i64
// SLANG-SAME: step_count = 2 : i64

// TALOS: talos.sv.statement.for_loop attributes {
// TALOS-SAME: has_condition = true
// TALOS-SAME: initializer_count = 2 : i64
// TALOS-SAME: step_count = 2 : i64
// TALOS: talos.sv.statement.for_loop attributes {
// TALOS-SAME: has_condition = false
// TALOS-SAME: initializer_count = 0 : i64
// TALOS-SAME: step_count = 0 : i64
// TALOS: talos.sv.statement.for_loop attributes {
// TALOS-SAME: has_condition = true
// TALOS-SAME: initializer_count = 0 : i64
// TALOS-SAME: step_count = 2 : i64
