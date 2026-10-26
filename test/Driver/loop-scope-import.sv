// RUN: obelisk -emit-slang %s -o - | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s -o - | FileCheck %s --check-prefix=SEMANTIC
module top;
  int data[2];
  int k;
  initial begin
    sweep: for (int i = 0; i < 2; i++) data[i] = i;
    for (k = 0; k < 2; k++) data[k] = k;
    each: foreach (data[j]) data[j] = j;
  end
endmodule
// SLANG: slang.statement.block attributes {{.*}}is_implicit_loop_scope
// SLANG: slang.statement.for_loop attributes {{.*}}has_local_variables = true
// SLANG: slang.statement.for_loop attributes {has_condition = true, initializer_count = 1 : i64
// SLANG: slang.statement.block attributes {{.*}}is_implicit_loop_scope
// SLANG: slang.statement.foreach_loop
// SEMANTIC: obelisk.sv.statement.block attributes {{.*}}is_implicit_loop_scope
// SEMANTIC: obelisk.sv.statement.for_loop attributes {{.*}}has_local_variables = true
// SEMANTIC: obelisk.sv.statement.for_loop attributes {has_condition = true, initializer_count = 1 : i64
// SEMANTIC: obelisk.sv.statement.block attributes {{.*}}is_implicit_loop_scope
// SEMANTIC: obelisk.sv.statement.foreach_loop
