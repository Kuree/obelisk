// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: %if compiled-sampler %{ obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s --check-prefix=CHECK --check-prefix=PLAN %}
// RUN: %if !compiled-sampler %{ obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s --check-prefix=CHECK --check-prefix=FALLBACK %}
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Static random variables participate in ordinary object.randomize(). Their
// rand_mode state is class-wide, just like a static randc sequence. A
// class-wide rand_mode call dispatches on the dynamic class so it updates the
// inherited static state without changing an unrelated class's state.
// CHECK: simulation.storage.decl 0 in 0 : i32 design hierarchy "C::s" debug "s"
// CHECK: simulation.storage.decl 1 in 0 : !simulation.packed_array<1 : 0 x i1> design hierarchy "C::cycle" debug "cycle"
// CHECK: simulation.storage.decl 2 in 0 : i32 design hierarchy "D::ds" debug "ds"
// CHECK: simulation.storage.decl 6 in 0 : i64 design hierarchy "C::s.$rand_mode" debug "__obelisk_rand_mode"
// CHECK: simulation.storage.decl 7 in 0 : i64 design hierarchy "C::cycle.$rand_mode" debug "__obelisk_rand_mode"
// CHECK: simulation.storage.decl 8 in 0 : i64 design hierarchy "D::ds.$rand_mode" debug "__obelisk_rand_mode"
// CHECK: simulation.storage.decl 9 in 0 : i64 design hierarchy "C::cycle.$randc_key" debug "__obelisk_static_randc_key"
// CHECK: simulation.storage.decl 10 in 0 : i64 design hierarchy "C::cycle.$randc_position" debug "__obelisk_static_randc_position"
// An unqualified random property inside an instance method uses the method's
// implicit this object for both the task and function forms of rand_mode.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK-SAME: %[[IMPLICIT_THIS:arg[0-9]+]]: !simulation.class_handle<@__obelisk_class_s3_C>
// CHECK-SAME: simulation.hierarchical_name = "C::implicit_modes"
// CHECK: %[[IMPLICIT_MODE_REF:.*]] = simulation.class.field_ref %[[IMPLICIT_THIS]][@__obelisk_class_s3_C_field___obelisk_rand_mode]
// CHECK: simulation.managed.store {{.*}} to %[[IMPLICIT_MODE_REF]]
// CHECK: %[[IMPLICIT_MODE:.*]] = simulation.managed.load %[[IMPLICIT_MODE_REF]]
// CHECK: arith.andi %[[IMPLICIT_MODE]],
// CHECK-LABEL: simulation.func private @unit_3
// CHECK-SAME: %[[S_VALUE:arg[0-9]+]]: !simulation.ref<i32>
// CHECK-SAME: %[[CYCLE_VALUE:arg[0-9]+]]: !simulation.ref<!simulation.packed_array<1 : 0 x i1>>
// CHECK-SAME: %[[DS_VALUE:arg[0-9]+]]: !simulation.ref<i32>
// CHECK-SAME: %[[OBJECT_REF:arg[0-9]+]]: !simulation.ref<!simulation.class_handle<@__obelisk_class_s3_C>>
// CHECK: %[[S_MODE:.*]] = simulation.context.storage %arg0[6] : !simulation.ref<i64>
// CHECK-NEXT: simulation.ref.store {{.*}} to %[[S_MODE]] : i64, !simulation.ref<i64>
// CHECK-NEXT: simulation.ref.store {{.*}} to %[[S_MODE]] : i64, !simulation.ref<i64>
// CHECK-NEXT: %[[S_MODE_VALUE:.*]] = simulation.ref.load %[[S_MODE]] : !simulation.ref<i64> -> i64
// CHECK: %[[MODE_OBJECT:.*]] = simulation.ref.load %[[OBJECT_REF]]
// CHECK-NEXT: %[[OBJECT_MODE:.*]] = simulation.class.field_ref %[[MODE_OBJECT]][@__obelisk_class_s3_C_field___obelisk_rand_mode]
// CHECK-NEXT: %[[MODE_IS_D:.*]] = simulation.class.is_instance %[[MODE_OBJECT]] is @__obelisk_class_s22_D
// CHECK-NEXT: cf.cond_br %[[MODE_IS_D]], ^[[D_MODE:bb[0-9]+]], ^[[MODE_NEXT:bb[0-9]+]]
// CHECK: ^[[MODE_DONE:bb[0-9]+]]:
// CHECK-NEXT: %[[RANDOM_OBJECT:.*]] = simulation.ref.load %[[OBJECT_REF]]
// CHECK-NEXT: %[[RANDOM_IS_D:.*]] = simulation.class.is_instance %[[RANDOM_OBJECT]] is @__obelisk_class_s22_D
// CHECK-NEXT: cf.cond_br %[[RANDOM_IS_D]], ^[[D_RANDOM:bb[0-9]+]], ^[[C_RANDOM_TEST:bb[0-9]+]]
// CHECK: ^[[D_MODE]]:
// CHECK: simulation.managed.store {{.*}} to %[[OBJECT_MODE]]
// CHECK: simulation.ref.store {{.*}} to %[[S_MODE]] : i64, !simulation.ref<i64>
// CHECK: %[[CYCLE_MODE_D:.*]] = simulation.context.storage %arg0[7]
// CHECK: simulation.ref.store {{.*}} to %[[CYCLE_MODE_D]]
// CHECK: %[[DS_MODE_D:.*]] = simulation.context.storage %arg0[8]
// CHECK: simulation.ref.store {{.*}} to %[[DS_MODE_D]]
// CHECK: cf.br ^[[MODE_DONE]]
// CHECK: ^[[MODE_NEXT]]:
// CHECK-NEXT: %[[MODE_IS_C:.*]] = simulation.class.is_instance %[[MODE_OBJECT]] is @__obelisk_class_s3_C
// CHECK: ^[[C_MODE:bb[0-9]+]]:
// CHECK: simulation.managed.store {{.*}} to %[[OBJECT_MODE]]
// CHECK: simulation.ref.store {{.*}} to %[[S_MODE]] : i64, !simulation.ref<i64>
// CHECK: %[[CYCLE_MODE_C:.*]] = simulation.context.storage %arg0[7]
// CHECK: simulation.ref.store {{.*}} to %[[CYCLE_MODE_C]]
// CHECK-NOT: simulation.context.storage %arg0[8]
// CHECK: cf.br ^[[MODE_DONE]]
// Ordinary randomization sees all five effective properties in D: the three
// shared static mode bits replace their object-mask bits, and successful
// solving commits the shared static values and shared randc state.
// CHECK: ^[[D_RANDOM]]:
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s3_C_field_2]
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s22_D_field_1]
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s3_C_field___obelisk_rand_mode]
// CHECK: arith.andi {{.*}}, {{.*}} : i64
// CHECK: %{{.*}} = simulation.ref.load %[[S_MODE]]
// CHECK: %[[D_CYCLE_MODE:.*]] = simulation.context.storage %arg0[7]
// CHECK-NEXT: %{{.*}} = simulation.ref.load %[[D_CYCLE_MODE]]
// CHECK: %[[D_DS_MODE:.*]] = simulation.context.storage %arg0[8]
// CHECK-NEXT: %{{.*}} = simulation.ref.load %[[D_DS_MODE]]
// CHECK: simulation.ref.load %[[S_VALUE]] : !simulation.ref<i32>
// CHECK: simulation.ref.load %[[CYCLE_VALUE]]
// PLAN: simulation.ref.load %[[DS_VALUE]] : !simulation.ref<i32>
// CHECK: ^[[C_RANDOM_TEST]]:
// CHECK: simulation.class.is_instance %[[RANDOM_OBJECT]] is @__obelisk_class_s3_C
// FALLBACK: simulation.ref.load %[[DS_VALUE]] : !simulation.ref<i32>
// CHECK: simulation.ref.store {{.*}} to %[[S_VALUE]] : i32, !simulation.ref<i32>
// CHECK: simulation.ref.store {{.*}} to %[[CYCLE_VALUE]]
// CHECK: simulation.ref.store {{.*}} to %[[DS_VALUE]] : i32, !simulation.ref<i32>

class C;
  static rand int s;
  static randc bit [1:0] cycle;
  rand int x;
  function int implicit_modes();
    x.rand_mode(0);
    return x.rand_mode();
  endfunction
endclass
class D extends C;
  static rand int ds;
  rand int y;
endclass
module top;
  D derived = new;
  C object = derived;
  int result;
  initial begin
    C::s.rand_mode(1);
    object.s.rand_mode(0);
    result = object.s.rand_mode();
    object.rand_mode(0);
    result = object.randomize();
  end
endmodule
