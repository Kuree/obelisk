// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK
// RUN: obelisk -O0 -emit-sim %s | FileCheck %s --check-prefix=SIM

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

// OBELISK: obelisk.sv.statement.for_loop attributes {
// OBELISK-SAME: has_condition = true
// OBELISK-SAME: initializer_count = 2 : i64
// OBELISK-SAME: step_count = 2 : i64
// OBELISK: obelisk.sv.statement.for_loop attributes {
// OBELISK-SAME: has_condition = false
// OBELISK-SAME: initializer_count = 0 : i64
// OBELISK-SAME: step_count = 0 : i64
// OBELISK: obelisk.sv.statement.for_loop attributes {
// OBELISK-SAME: has_condition = true
// OBELISK-SAME: initializer_count = 0 : i64
// OBELISK-SAME: step_count = 2 : i64

// SIM-NOT: obelisk.sv.statement.for_loop
// SIM: cf.cond_br
// SIM-NOT: obelisk.sv.statement.for_loop
