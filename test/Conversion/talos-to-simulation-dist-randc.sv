// RUN: %split-file %s %t
// RUN: talos -emit-slang %t/dist.sv | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\"}"' '--lower-talos-to-sim=opt-level=0' -o %t/dist.mlir
// RUN: FileCheck %s --check-prefix=DIST < %t/dist.mlir
// RUN: talos-opt %t/dist.mlir '--encode-talos-sim-to-bytecode=vpi=off' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: talos -emit-slang %t/dist-signed.sv | talos-opt --convert-slang-to-talos '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=DIST-SIGNED
// RUN: talos -emit-slang %t/randc.sv | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\"}"' '--lower-talos-to-sim=opt-level=0' -o %t/randc.mlir
// RUN: FileCheck %s --check-prefix=RANDC --check-prefix=%if compiled-sampler %{RANDC-PLAN%} %else %{RANDC-FALLBACK%} < %t/randc.mlir
// RUN: talos-opt %t/randc.mlir '--encode-talos-sim-to-bytecode=vpi=off' --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=RANDC-NATIVE --check-prefix=%if compiled-sampler %{RANDC-NATIVE-PLAN%} %else %{RANDC-NATIVE-FALLBACK%}

// The runtime weight w of [1:3] := w is zero-extended (IEEE 1800-2023 18.5.3
// interprets weights as unsigned bit patterns) and captured as a solver input;
// the weight arithmetic is evaluated by the sampling plan, not lowered to IR.
// DIST: simulation.class.field @[[W_FIELD:[^ ]+]] {{.*}} : i32 {debug_name = "w"
// DIST: %[[W_REF:.*]] = simulation.class.field_ref {{.*}}[@[[W_FIELD]]]
// DIST: %[[W:.*]] = simulation.managed.load %[[W_REF]]
// DIST: %[[W64:.*]] = arith.extui %[[W]] : i32 to i64
// DIST-NOT: arith.cmpi sge, %[[W64]]
// DIST-NOT: arith.muli %[[W64]]
// DIST: simulation.random.solve {{.*}} captures({{.*}}, %[[W64]], {{.*}})

// A soft dist (18.5.13) and a dist combined with solve-before (18.5.9) are
// executable: each lowers to a solve capturing the literal weights 1 and 6.
// DIST: simulation.random.solve {{.*}} captures(%{{c1_i64[^,]*}}, %{{c6_i64[^)]*}})
// DIST: simulation.random.solve {{.*}} captures(%{{c1_i64[^,]*}}, %{{c6_i64[^)]*}})

// DIST-SIGNED: arith.xori {{.*}}, {{.*}} : i64
// DIST-SIGNED: simulation.random.solve

// RANDC: simulation.class.field @[[VALUE:[^ ]+]] {{.*}}simulation.random_cycle_key_field = @[[KEY:[^, }]+]]{{.*}}simulation.random_cycle_position_field = @[[POSITION:[^, }]+]]{{.*}}simulation.random_variable_kind = 2 : i32
// RANDC: simulation.class.field @[[KEY]] {{.*}} {debug_name = "__talos_randc_key"
// RANDC: simulation.class.field @[[POSITION]] {{.*}} {debug_name = "__talos_randc_position"
// RANDC: %[[KEY_REF:.*]] = simulation.class.field_ref {{.*}}[@[[KEY]]]
// RANDC: %[[POSITION_REF:.*]] = simulation.class.field_ref {{.*}}[@[[POSITION]]]
// RANDC: %[[KEY_VALUE:.*]] = simulation.managed.load %[[KEY_REF]]
// RANDC: %[[POSITION_VALUE:.*]] = simulation.managed.load %[[POSITION_REF]]
// RANDC-PLAN: cf.cond_br %{{[^, ]+}}, ^[[PROBE:bb[0-9]+]]({{[^)]*}}, %[[KEY_VALUE]], %[[POSITION_VALUE]],
// RANDC-PLAN: ^[[PROBE]]({{.*}}, %[[STAGED_KEY:[^: ]+]]: i64, %[[STAGED_POSITION:[^: ]+]]: i64, %[[STAGED_DOMAIN:[^: ]+]]: !simulation.string):
// RANDC-PLAN: simulation.random.cycle_sample {{.*}}, %[[STAGED_KEY]], %[[STAGED_POSITION]], %[[STAGED_DOMAIN]],
// RANDC-PLAN-SAME: SAMP
// RANDC-FALLBACK: cf.br {{.*}}%[[POSITION_VALUE]]
// RANDC-FALLBACK: %[[NEXT_POSITION:.*]], %[[VALUE:.*]] = simulation.random.cycle_next {{.*}} {width = 4 : i32}
// RANDC-NOT: simulation.managed.store {{.*}} to %[[POSITION_REF]]
// RANDC: simulation.random.solve
// RANDC: simulation.managed.store {{.*}} to %[[KEY_REF]]
// RANDC-NEXT: simulation.managed.store {{.*}} to %[[POSITION_REF]]

// RANDC-NATIVE: llvm.func @talos_rt_v1_random_cycle_next
// RANDC-NATIVE-PLAN: %[[CYCLE_STATUS:.*]] = llvm.call @talos_rt_v1_random_cycle_sample
// RANDC-NATIVE-FALLBACK: %[[CYCLE_STATUS:.*]] = llvm.call @talos_rt_v1_random_cycle_next
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
class S;
  rand bit [3:0] x;
  constraint c { soft x dist {0 := 1, [1:3] :/ 6}; }
endclass
class V;
  rand bit [3:0] x;
  rand bit [3:0] y;
  constraint c {
    solve x before y;
    y dist {0 := 1, [1:3] :/ 6};
  }
endclass
module top;
  initial begin
    automatic C c = new;
    automatic S s = new;
    automatic V v = new;
    void'(c.randomize());
    void'(s.randomize());
    void'(v.randomize());
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
