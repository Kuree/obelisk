// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/wide-unconstrained.sv | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=WIDE
// RUN: %if z3 %{ \
// RUN:   obelisk -emit-slang %t/wide-equality.sv | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=EQUALITY \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk -emit-slang %t/wide-table.sv | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=WIDE-TABLE \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk -emit-slang %t/wide-residual.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' - \
// RUN:     '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=RESIDUAL \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk -emit-slang %t/wide-residual.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' - \
// RUN:     '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:     | FileCheck %s --check-prefix=RESIDUAL-NATIVE \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk -emit-slang %t/wide-residual.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' - \
// RUN:     '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null \
// RUN: %}

// WIDE-LABEL: simulation.func private @unit_1
// WIDE-COUNT-4: arith.muli
// WIDE: %[[TOP:.*]] = arith.andi {{.*}}, {{.*}} : i64
// WIDE-NEXT: arith.extui %[[TOP]] : i64 to i65
// WIDE: arith.shli {{.*}} : i65
// WIDE: simulation.managed.store
// WIDE-NOT: simulation.random.solve

// EQUALITY-LABEL: simulation.func private @unit_1
// EQUALITY: arith.constant 18446744073709551620 : i128
// EQUALITY: arith.cmpi eq, {{.*}} : i128
// EQUALITY-NOT: simulation.random.solve %

// WIDE-TABLE-LABEL: simulation.func private @unit_1
// WIDE-TABLE: %[[ALL_RAND:.*]] = arith.cmpi eq, {{.*}} : i64
// WIDE-TABLE: arith.cmpi eq, {{.*}} : i64
// WIDE-TABLE: %[[ALL_CONSTRAINTS:.*]] = arith.cmpi eq, {{.*}} : i64
// WIDE-TABLE: %[[USE_PLAN:.*]] = arith.andi %[[ALL_CONSTRAINTS]], %[[ALL_RAND]] : i1
// WIDE-TABLE: cf.cond_br %[[USE_PLAN]],
// WIDE-TABLE: arith.trunci {{.*}} : i128 to i64
// WIDE-TABLE: arith.remui {{.*}}, {{.*}} : i64
// WIDE-TABLE: arith.cmpi ult, {{.*}} : i64
// WIDE-TABLE: arith.extui {{.*}} : i64 to i128
// WIDE-TABLE: %[[ROW:.*]] = arith.remui {{.*}}, {{.*}} : i128
// WIDE-TABLE: arith.cmpi eq, %[[ROW]], {{.*}} : i128
// WIDE-TABLE: arith.select {{.*}}, {{.*}}, {{.*}} : i128

// RESIDUAL-LABEL: simulation.func private @unit_1
// RESIDUAL: simulation.random.solve_wide
// RESIDUAL-SAME: : (!simulation.context, i128, i128, i64, i64, i64, i64, i128) -> (i128, i1, i64)

// RESIDUAL-NATIVE: llvm.call @obelisk_rt_v1_random_solve_wide_modes_state

//--- wide-unconstrained.sv
class C;
  rand bit [64:0] value;
endclass
module top;
  C object = new;
  int result;
  initial result = object.randomize();
endmodule

//--- wide-equality.sv
class C;
  rand bit [127:0] value;
  constraint impossible {
    value == 128'd18446744073709551617 + 128'd3;
  }
endclass
module top;
  C object = new;
  int result;
  initial result = object.randomize();
endmodule

//--- wide-table.sv
class C;
  rand bit [127:0] value;
  constraint below_three {
    value < 128'd3;
  }
endclass
module top;
  C object = new;
  int result;
  initial result = object.randomize();
endmodule

//--- wide-residual.sv
class C;
  rand bit [127:0] value;
  constraint impossible {
    value != 128'd18446744073709551617 + 128'd3;
  }
endclass
module top;
  C object = new;
  int result;
  initial result = object.randomize();
endmodule
