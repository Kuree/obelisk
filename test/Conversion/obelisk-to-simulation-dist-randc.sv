// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/dist.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\"}"' '--lower-obelisk-to-sim=opt-level=0' -o %t/dist.mlir
// RUN: FileCheck %s --check-prefix=DIST < %t/dist.mlir
// RUN: obelisk-opt %t/dist.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk -emit-slang %t/dist-signed.sv | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=DIST-SIGNED
// RUN: obelisk -emit-slang %t/randc.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\"}"' '--lower-obelisk-to-sim=opt-level=0' -o %t/randc.mlir
// RUN: FileCheck %s --check-prefix=RANDC < %t/randc.mlir
// RUN: obelisk-opt %t/randc.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=RANDC-NATIVE

// The runtime weight w of [1:3] := w is checked and scaled by the common mass
// factor of the per-range items ([4:7] :/ 8 and default :/ 4 over the eight
// unlisted values); the constant items total 104.
// DIST: simulation.class.field @[[W_FIELD:[^ ]+]] {{.*}} : i32 {debug_name = "w"
// DIST: %[[W_REF:.*]] = simulation.class.field_ref {{.*}}[@[[W_FIELD]]]
// DIST: %[[W:.*]] = simulation.managed.load %[[W_REF]]
// DIST: %[[W64:.*]] = arith.extsi %[[W]] : i32 to i64
// DIST: arith.cmpi sge, %[[W64]]
// DIST: %[[MASS:.*]] = arith.constant {{.*}} 24 : i64
// DIST: arith.muli %[[W64]], %[[MASS]]
// DIST: arith.divui
// DIST: arith.constant {{.*}} 104 : i64
// DIST: simulation.random.solve {{.*}} captures({{.*}}, %[[W64]], {{.*}})

// DIST-SIGNED: arith.xori {{.*}}, {{.*}} : i64
// DIST-SIGNED: simulation.random.solve

// RANDC: simulation.class.field @[[VALUE:[^ ]+]] {{.*}}simulation.random_cycle_key_field = @[[KEY:[^, }]+]]{{.*}}simulation.random_cycle_position_field = @[[POSITION:[^, }]+]]{{.*}}simulation.random_variable_kind = 2 : i32
// RANDC: simulation.class.field @[[KEY]] {{.*}} {debug_name = "__obelisk_randc_key"
// RANDC: simulation.class.field @[[POSITION]] {{.*}} {debug_name = "__obelisk_randc_position"
// RANDC: %[[KEY_REF:.*]] = simulation.class.field_ref {{.*}}[@[[KEY]]]
// RANDC: %[[POSITION_REF:.*]] = simulation.class.field_ref {{.*}}[@[[POSITION]]]
// RANDC: %[[KEY_VALUE:.*]] = simulation.managed.load %[[KEY_REF]]
// RANDC: %[[POSITION_VALUE:.*]] = simulation.managed.load %[[POSITION_REF]]
// RANDC: cf.br {{.*}}%[[POSITION_VALUE]]
// RANDC: %[[NEXT_POSITION:.*]], %[[VALUE:.*]] = simulation.random.cycle_next {{.*}} {width = 4 : i32}
// RANDC-NOT: simulation.managed.store {{.*}} to %[[POSITION_REF]]
// RANDC: simulation.random.solve
// RANDC: simulation.managed.store {{.*}} to %[[KEY_REF]]
// RANDC-NEXT: simulation.managed.store {{.*}} to %[[POSITION_REF]]

// RANDC-NATIVE: llvm.func @obelisk_rt_v1_random_cycle_next
// RANDC-NATIVE: %[[CYCLE_STATUS:.*]] = llvm.call @obelisk_rt_v1_random_cycle_next
// RANDC-NATIVE: %[[CYCLE_OK:.*]] = llvm.icmp "eq" %[[CYCLE_STATUS]],
// RANDC-NATIVE: llvm.cond_br %[[CYCLE_OK]],

//--- dist.sv
class C;
  rand bit [3:0] x;
  int w = 2;
  constraint c {
    x dist {0 := 1, [1:3] := w, [4:7] :/ 8, default :/ 4};
  }
endclass
module top;
  initial begin
    automatic C c = new;
    void'(c.randomize());
  end
endmodule

//--- dist-signed.sv
class C;
  rand bit signed [3:0] x;
  constraint c {
    x dist {[-3:-1] := 2, 0 := 1};
  }
endclass
module top;
  initial begin
    automatic C c = new;
    void'(c.randomize());
  end
endmodule

//--- randc.sv
class C;
  randc bit [3:0] x;
  rand bit [3:0] y;
  constraint c {
    y != x;
  }
endclass
module top;
  initial begin
    automatic C c = new;
    void'(c.randomize());
  end
endmodule
