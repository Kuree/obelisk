// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-p:64:64-i64:64-i32:32-i16:16-i8:8\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// The object stream chooses the first assignment for a bounded generated
// sampler. Exhaustion invokes the compiler-serialized runtime program, and the
// rand property is stored only on a successful commit edge.
// CHECK: simulation.class.decl @[[CLASS:[A-Za-z0-9_.$]+]]
// CHECK-SAME: random_constraint_template = @[[TEMPLATE:[A-Za-z0-9_.$]+]]
// CHECK: simulation.class.field {{.*}}debug_name = "value"
// CHECK: simulation.class.field {{.*}}debug_name = "__obelisk_rng_state"
// CHECK: simulation.class.field {{.*}}debug_name = "__obelisk_rng_increment"
// CHECK: simulation.class.field {{.*}}debug_name = "__obelisk_rand_mode"
// CHECK: simulation.class.field {{.*}}debug_name = "__obelisk_constraint_mode"
// CHECK: simulation.random.constraint_template @[[TEMPLATE]] of @[[CLASS]]
// CHECK-SAME: constraint_blocks = [#simulation.random_constraint_block_reference<kind = object_block, index = 0 : i32>]
// CHECK-SAME: references = [#simulation.random_value_reference<kind = object_field, target = @{{[^,]+}}, low = 0, width = 32>, #simulation.random_value_reference<kind = storage, storage = 0 : i64, low = 0, width = 32>]
// CHECK: %[[TEMPLATE_VALUE:.*]] = simulation.random.constraint_value 0 : i32
// CHECK: %[[TEMPLATE_LIMIT:.*]] = simulation.random.constraint_value 1 : i32
// CHECK: %[[TEMPLATE_GT:.*]] = arith.cmpi sgt, %[[TEMPLATE_VALUE]], %[[TEMPLATE_LIMIT]] : i32
// CHECK: simulation.random.soft_constraint %[[TEMPLATE_GT]] block 0 priority 0
// CHECK: simulation.random.soft_constraint %{{.*}} block 0 priority 1
// CHECK: simulation.func private @unit_1({{.*}}%[[LIMIT_ARG:arg[0-9]+]]: !simulation.ref<i32>
// CHECK: simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_rand_mode]
// CHECK: %[[OLD_MODE:.*]] = simulation.managed.load
// CHECK: %[[DISABLED_MODE:.*]] = arith.ori %[[OLD_MODE]], %{{c1_i64.*}} : i64
// CHECK: simulation.managed.store %[[DISABLED_MODE]]
// CHECK: simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_rand_mode]
// CHECK: %[[MODE:.*]] = simulation.managed.load
// CHECK: %[[PROPERTY_MODE:.*]] = arith.andi %[[MODE]], %{{c1_i64.*}} : i64
// CHECK: %[[MODE_ENABLED:.*]] = arith.cmpi eq, %[[PROPERTY_MODE]], %{{c0_i64.*}} : i64
// CHECK: arith.extui %[[MODE_ENABLED]] : i1 to i32
// CHECK: simulation.ref.store {{.*}} to %[[LIMIT_ARG]]
// CHECK: simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_rand_mode]
// CHECK: simulation.managed.store %{{c0_i64.*}}
// CHECK: simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: simulation.managed.store %{{c-1_i64.*}}
// CHECK: simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: %[[BLOCK_MODES:.*]] = simulation.managed.load
// CHECK: %[[BLOCK_MODE:.*]] = arith.andi %[[BLOCK_MODES]], %{{c1_i64.*}} : i64
// CHECK: %[[BLOCK_ENABLED:.*]] = arith.cmpi eq, %[[BLOCK_MODE]], %{{c0_i64.*}} : i64
// CHECK: arith.extui %[[BLOCK_ENABLED]] : i1 to i32
// CHECK: simulation.ref.store {{.*}} to %[[LIMIT_ARG]]
// CHECK: simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: %[[OLD_BLOCK_MODES:.*]] = simulation.managed.load
// CHECK: %[[ENABLED_BLOCK_MODES:.*]] = arith.andi %[[OLD_BLOCK_MODES]], %{{c-2_i64.*}} : i64
// CHECK: simulation.managed.store %[[ENABLED_BLOCK_MODES]]
// CHECK: %[[LIVE_BLOCK_MODE_REF:.*]] = simulation.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: %[[LIVE_BLOCK_MODES:.*]] = simulation.managed.load %[[LIVE_BLOCK_MODE_REF]]
// CHECK: %[[RELEVANT_MODE:.*]] = arith.andi
// CHECK-DAG: %[[ALL_PROPERTIES_ENABLED:.*]] = arith.cmpi eq, %[[RELEVANT_MODE]], %{{c0_i64.*}} : i64
// CHECK-DAG: %[[ALL_DISABLED:.*]] = arith.cmpi eq, %[[RELEVANT_MODE]], %{{c1_i64.*}} : i64
// CHECK-DAG: %[[RELEVANT_BLOCK_MODES:.*]] = arith.andi %[[LIVE_BLOCK_MODES]], %{{c1_i64.*}} : i64
// CHECK-DAG: %[[ALL_BLOCKS_ENABLED:.*]] = arith.cmpi eq, %[[RELEVANT_BLOCK_MODES]], %{{c0_i64.*}} : i64
// CHECK: arith.select
// CHECK: simulation.ref.load %[[LIMIT_ARG]]
// CHECK: cf.cond_br %[[ALL_DISABLED]]
// CHECK: arith.muli
// The creation guard has already excluded the all-frozen path, so the
// optimized proposal needs only the live constraint-block check here.
// CHECK: cf.cond_br %[[ALL_BLOCKS_ENABLED]]
// CHECK: arith.cmpi slt
// CHECK: arith.ori
// CHECK: arith.cmpi sgt
// CHECK: arith.select
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi sge
// CHECK: arith.cmpi sle
// CHECK: cf.cond_br
// CHECK: arith.cmpi uge
// CHECK: simulation.random.solve {{.*}} mutable {{.*}} constraints %[[RELEVANT_BLOCK_MODES]]
// CHECK: cf.cond_br
// CHECK: arith.trunci
// CHECK: cf.cond_br
// CHECK: simulation.managed.store
// CHECK-NOT: obelisk.sv.

int limit;
class constrained;
  rand int value;
  constraint bounds {
    soft value > limit;
    soft 1;
  }
endclass
module unsupported_constraint;
  constrained object = new;
  initial begin
    object.value.rand_mode(0);
    limit = object.value.rand_mode();
    object.rand_mode(1);
    object.constraint_mode(0);
    limit = object.bounds.constraint_mode();
    object.bounds.constraint_mode(1);
    void'(object.randomize() with {
      value < 4;
      value == 1 -> value < 4;
      if (value == 2) value < 4; else value > 0;
      value inside {[1:3]};
    });
  end
endmodule
