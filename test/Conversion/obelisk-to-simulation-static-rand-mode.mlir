// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
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
// CHECK-NEXT: %[[MODE_IS_D:.*]] = simulation.class.is_instance %[[MODE_OBJECT]] is @__obelisk_class_s19_D
// CHECK-NEXT: cf.cond_br %[[MODE_IS_D]], ^[[D_MODE:bb[0-9]+]], ^[[MODE_NEXT:bb[0-9]+]]
// CHECK: ^[[MODE_DONE:bb[0-9]+]]:
// CHECK-NEXT: %[[RANDOM_OBJECT:.*]] = simulation.ref.load %[[OBJECT_REF]]
// CHECK-NEXT: %[[RANDOM_IS_D:.*]] = simulation.class.is_instance %[[RANDOM_OBJECT]] is @__obelisk_class_s19_D
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
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s19_D_field_1]
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s3_C_field___obelisk_rand_mode]
// CHECK: arith.andi {{.*}}, {{.*}} : i64
// CHECK: %{{.*}} = simulation.ref.load %[[S_MODE]]
// CHECK: %[[D_CYCLE_MODE:.*]] = simulation.context.storage %arg0[7]
// CHECK-NEXT: %{{.*}} = simulation.ref.load %[[D_CYCLE_MODE]]
// CHECK: %[[D_DS_MODE:.*]] = simulation.context.storage %arg0[8]
// CHECK-NEXT: %{{.*}} = simulation.ref.load %[[D_DS_MODE]]
// CHECK: simulation.ref.load %[[S_VALUE]] : !simulation.ref<i32>
// CHECK: simulation.ref.load %[[CYCLE_VALUE]]
// CHECK: ^[[C_RANDOM_TEST]]:
// CHECK: simulation.class.is_instance %[[RANDOM_OBJECT]] is @__obelisk_class_s3_C
// CHECK: simulation.ref.load %[[DS_VALUE]] : !simulation.ref<i32>
// CHECK: simulation.ref.store {{.*}} to %[[S_VALUE]] : i32, !simulation.ref<i32>
// CHECK: simulation.ref.store {{.*}} to %[[CYCLE_VALUE]]
// CHECK: simulation.ref.store {{.*}} to %[[DS_VALUE]] : i32, !simulation.ref<i32>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 66 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s44.this} {
        obelisk.sv.symbol.class_property @s4.s attributes {hierarchical_name = "C::s", lifetime = 1 : i32, name = "s", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.class_property @s5.cycle attributes {hierarchical_name = "C::cycle", lifetime = 1 : i32, name = "cycle", node_id = 5 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.class_property @s6.x attributes {hierarchical_name = "C::x", name = "x", node_id = 6 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.subroutine @s45.implicit_modes attributes {hierarchical_name = "C::implicit_modes", name = "implicit_modes", node_id = 300 : i64, return_variable_path = "C::implicit_modes.implicit_modes", return_variable_symbol = @s1.$root::@s2::@s3.C::@s45.implicit_modes::@s46.implicit_modes, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, this_variable_path = "C::implicit_modes.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s45.implicit_modes::@s47.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 301 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 302 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 303 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "C::implicit_modes", system_scope_symbol = @s1.$root::@s2::@s3.C::@s45.implicit_modes} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 304 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 305 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.return attributes {node_id = 306 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 307 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "C::implicit_modes", system_scope_symbol = @s1.$root::@s2::@s3.C::@s45.implicit_modes} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 308 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s46.implicit_modes attributes {hierarchical_name = "C::implicit_modes.implicit_modes", is_compiler_generated, name = "implicit_modes", node_id = 309 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.variable @s47.this attributes {hierarchical_name = "C::implicit_modes.this", is_compiler_generated, is_const, name = "this", node_id = 310 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s7.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 7 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s9.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.get_randstate attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s11.set_randstate attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 15 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s12.state attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 17 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s13.srandom attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 18 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s14.seed attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s15.rand_mode attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s16.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s17.constraint_mode attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 24 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 25 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s18.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s44.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 86 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
      obelisk.sv.type.class_type @s19.D attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, bitstream_width = 130 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 27 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.D>, this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s19.D::@s43.this} {
        obelisk.sv.symbol.transparent_member @s20.x attributes {hierarchical_name = "D::x", name = "x", node_id = 28 : i64} {
        }
        obelisk.sv.symbol.transparent_member @s21.cycle attributes {hierarchical_name = "D::cycle", name = "cycle", node_id = 29 : i64} {
        }
        obelisk.sv.symbol.transparent_member @s22.s attributes {hierarchical_name = "D::s", name = "s", node_id = 30 : i64} {
        }
        obelisk.sv.symbol.class_property @s23.ds attributes {hierarchical_name = "D::ds", lifetime = 1 : i32, name = "ds", node_id = 31 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.class_property @s24.y attributes {hierarchical_name = "D::y", name = "y", node_id = 32 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.subroutine @s25.randomize attributes {hierarchical_name = "D::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 33 : i64, override_path = "C::randomize", override_symbol = @s1.$root::@s2::@s3.C::@s7.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 34 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s26.pre_randomize attributes {hierarchical_name = "D::pre_randomize", is_builtin, name = "pre_randomize", node_id = 35 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 36 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s27.post_randomize attributes {hierarchical_name = "D::post_randomize", is_builtin, name = "post_randomize", node_id = 37 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 38 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s28.get_randstate attributes {hierarchical_name = "D::get_randstate", is_builtin, name = "get_randstate", node_id = 39 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 40 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s29.set_randstate attributes {hierarchical_name = "D::set_randstate", is_builtin, name = "set_randstate", node_id = 41 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 42 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s30.state attributes {direction = 0 : i32, hierarchical_name = "D::set_randstate.state", name = "state", node_id = 43 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s31.srandom attributes {hierarchical_name = "D::srandom", is_builtin, name = "srandom", node_id = 44 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 45 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s32.seed attributes {direction = 0 : i32, hierarchical_name = "D::srandom.seed", name = "seed", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s33.rand_mode attributes {hierarchical_name = "D::rand_mode", is_builtin, name = "rand_mode", node_id = 47 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 48 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s34.on_ff attributes {direction = 0 : i32, hierarchical_name = "D::rand_mode.on_ff", name = "on_ff", node_id = 49 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s35.constraint_mode attributes {hierarchical_name = "D::constraint_mode", is_builtin, name = "constraint_mode", node_id = 50 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 51 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s36.on_ff attributes {direction = 0 : i32, hierarchical_name = "D::constraint_mode.on_ff", name = "on_ff", node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s43.this attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 85 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.D>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s37.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 53 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s38.top attributes {hierarchical_name = "top", name = "top", node_id = 54 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s39.derived attributes {hierarchical_name = "top.derived", lifetime = 1 : i32, name = "derived", node_id = 55 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.D>} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 56 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.D>} {
          }
        }
        obelisk.sv.symbol.variable @s40.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 57 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 58 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "top.derived", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s39.derived, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.D>} {
            }
          }
        }
        obelisk.sv.symbol.variable @s41.result attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s42 attributes {hierarchical_name = "top", node_id = 61 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 62 : i64} {
            obelisk.sv.statement.list attributes {node_id = 63 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 200 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 201 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s37.top::@s38.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 202 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.s, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 203 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 64 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 65 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s37.top::@s38.top} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 63 : i64, is_signed = true, node_id = 66 : i64, packed_offset = 4294967296 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.s, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s40.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 69 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 70 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 71 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s41.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s37.top::@s38.top} {
                    obelisk.sv.expression.member_access attributes {field_ordinal = 63 : i64, is_signed = true, node_id = 73 : i64, packed_offset = 4294967296 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.s, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 74 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s40.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 75 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 76 : i64, referenced_path = "C::rand_mode", referenced_symbol = @s1.$root::@s2::@s3.C::@s15.rand_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 77 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s40.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 78 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 79 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 81 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 82 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s41.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 83 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s37.top::@s38.top} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 84 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s37.top::@s38.top::@s40.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
