// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "leaf", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "leaf", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s3.leaf", this_variable_path = "leaf::this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s52.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf::data", name = "data", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>, sym_name = "s5.data"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 6 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s6.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 7 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::pre_randomize", is_builtin, name = "pre_randomize", node_id = 8 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s7.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::post_randomize", is_builtin, name = "post_randomize", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 11 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::get_randstate", is_builtin, name = "get_randstate", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s9.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::set_randstate", is_builtin, name = "set_randstate", node_id = 14 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::set_randstate.state", name = "state", node_id = 16 : i64, semantic_type = !obelisk.string, sym_name = "s11.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::srandom", is_builtin, name = "srandom", node_id = 17 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::srandom.seed", name = "seed", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s13.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::rand_mode", is_builtin, name = "rand_mode", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::rand_mode.on_ff", name = "on_ff", node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s15.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::constraint_mode", is_builtin, name = "constraint_mode", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s16.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::constraint_mode.on_ff", name = "on_ff", node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s17.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf::this", is_compiler_generated, is_const, name = "this", node_id = 79 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s52.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "middle", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "middle", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s18.middle>, sym_name = "s18.middle", this_variable_path = "middle::this", this_variable_symbol = @s1.$root::@s2::@s18.middle::@s51.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "middle::l", name = "l", node_id = 27 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s19.l"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s20.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::pre_randomize", is_builtin, name = "pre_randomize", node_id = 30 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s21.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::post_randomize", is_builtin, name = "post_randomize", node_id = 32 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s22.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 33 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::get_randstate", is_builtin, name = "get_randstate", node_id = 34 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s23.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 35 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::set_randstate", is_builtin, name = "set_randstate", node_id = 36 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::set_randstate.state", name = "state", node_id = 38 : i64, semantic_type = !obelisk.string, sym_name = "s25.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::srandom", is_builtin, name = "srandom", node_id = 39 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 40 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::srandom.seed", name = "seed", node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s27.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::rand_mode", is_builtin, name = "rand_mode", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::rand_mode.on_ff", name = "on_ff", node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s29.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::constraint_mode", is_builtin, name = "constraint_mode", node_id = 45 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s30.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 46 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::constraint_mode.on_ff", name = "on_ff", node_id = 47 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s31.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "middle::this", is_compiler_generated, is_const, name = "this", node_id = 78 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s18.middle>, sym_name = "s51.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "root", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "root", node_id = 48 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s32.root>, sym_name = "s32.root", this_variable_path = "root::this", this_variable_symbol = @s1.$root::@s2::@s32.root::@s50.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "root::m", name = "m", node_id = 49 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s18.middle>, sym_name = "s33.m"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 50 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s34.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 51 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::pre_randomize", is_builtin, name = "pre_randomize", node_id = 52 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s35.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 53 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::post_randomize", is_builtin, name = "post_randomize", node_id = 54 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s36.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 55 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::get_randstate", is_builtin, name = "get_randstate", node_id = 56 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s37.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 57 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::set_randstate", is_builtin, name = "set_randstate", node_id = 58 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s38.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 59 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::set_randstate.state", name = "state", node_id = 60 : i64, semantic_type = !obelisk.string, sym_name = "s39.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::srandom", is_builtin, name = "srandom", node_id = 61 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s40.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 62 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::srandom.seed", name = "seed", node_id = 63 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s41.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::rand_mode", is_builtin, name = "rand_mode", node_id = 64 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s42.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 65 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::rand_mode.on_ff", name = "on_ff", node_id = 66 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s43.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::constraint_mode", is_builtin, name = "constraint_mode", node_id = 67 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s44.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 68 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::constraint_mode.on_ff", name = "on_ff", node_id = 69 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s45.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "root::this", is_compiler_generated, is_const, name = "this", node_id = 77 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s32.root>, sym_name = "s50.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 70 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s46.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 71 : i64, sym_name = "s47.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 72 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s32.root>, sym_name = "s48.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 73 : i64, procedure_kind = 0 : i32, sym_name = "s49", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 74 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s46.top::@s47.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s46.top::@s47.top::@s48.r, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s32.root>} {
              }
            }
          }
        }
      }
    }
  }
}


// A monomorphic recursive rand-object path contributes its packed leaves to
// the root's single assignment and randomizes existing container elements.
// Every handle remains unchanged. A null intermediate object or a disabled
// rand handle/leaf removes only that descendant from the mutable set.
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// CHECK: %[[MIDDLE_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s32_root_field_0]
// CHECK: %[[MIDDLE:.*]] = obelisk_sim.managed.load %[[MIDDLE_REF]]
// CHECK: %[[MIDDLE_NULL:.*]] = obelisk_sim.managed.is_null %[[MIDDLE]]
// CHECK: cf.cond_br %[[MIDDLE_NULL]], ^[[VALUE_MERGE:bb[0-9]+]]({{.*}}%false{{.*}}), ^[[MIDDLE_ACTIVE:bb[0-9]+]]
// CHECK: ^[[MIDDLE_ACTIVE]]:
// CHECK: %[[MIDDLE_MODE_REF:.*]] = obelisk_sim.class.field_ref %[[MIDDLE]][@__obelisk_class_s18_middle_field___obelisk_rand_mode]
// CHECK: %[[MIDDLE_MODES:.*]] = obelisk_sim.managed.load %[[MIDDLE_MODE_REF]]
// CHECK: arith.andi %[[MIDDLE_MODES]], {{.*}}1
// CHECK: %[[LEAF_REF:.*]] = obelisk_sim.class.field_ref %[[MIDDLE]][@__obelisk_class_s18_middle_field_0]
// CHECK: %[[LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]]
// CHECK: %[[LEAF_NULL:.*]] = obelisk_sim.managed.is_null %[[LEAF]]
// CHECK: arith.andi {{.*}}, {{.*}} : i1
// CHECK: cf.cond_br {{.*}}, ^[[LEAF_ACTIVE:bb[0-9]+]], ^[[VALUE_MERGE]]({{.*}}%false{{.*}})
// CHECK: ^[[VALUE_MERGE]]({{.*}}, %[[PATH_ENABLED:.*]]: i1):
// CHECK: arith.andi {{.*}}, %[[PATH_ENABLED]] : i1
// CHECK: ^[[LEAF_ACTIVE]]:
// CHECK: %[[X_REF:.*]] = obelisk_sim.class.field_ref %[[LEAF]][@__obelisk_class_s3_leaf_field_0]
// CHECK: obelisk_sim.managed.load %[[X_REF]]
// CHECK: %[[LEAF_MODE_REF:.*]] = obelisk_sim.class.field_ref %[[LEAF]][@__obelisk_class_s3_leaf_field___obelisk_rand_mode]
// CHECK: %[[LEAF_MODES:.*]] = obelisk_sim.managed.load %[[LEAF_MODE_REF]]
// CHECK: arith.andi %[[LEAF_MODES]], {{.*}}1
// CHECK: %[[COMMIT_MIDDLE:.*]] = obelisk_sim.managed.load %[[MIDDLE_REF]]
// CHECK: %[[COMMIT_LEAF_REF:.*]] = obelisk_sim.class.field_ref %[[COMMIT_MIDDLE]][@__obelisk_class_s18_middle_field_0]
// CHECK: %[[COMMIT_LEAF:.*]] = obelisk_sim.managed.load %[[COMMIT_LEAF_REF]]
// CHECK: %[[COMMIT_X_REF:.*]] = obelisk_sim.class.field_ref %[[COMMIT_LEAF]][@__obelisk_class_s3_leaf_field_0]
// CHECK: obelisk_sim.managed.store {{.*}} to %[[COMMIT_X_REF]]
// CHECK: %[[CONTAINER_MIDDLE:.*]] = obelisk_sim.managed.load %[[MIDDLE_REF]]
// CHECK: obelisk_sim.managed.is_null %[[CONTAINER_MIDDLE]]
// CHECK: obelisk_sim.container.write [[DATA:%[^,]+]],
// CHECK: obelisk_sim.class.field_ref %[[CONTAINER_MIDDLE]][@__obelisk_class_s18_middle_field___obelisk_rand_mode]
// CHECK: %[[CONTAINER_LEAF_REF:.*]] = obelisk_sim.class.field_ref %[[CONTAINER_MIDDLE]][@__obelisk_class_s18_middle_field_0]
// CHECK: %[[CONTAINER_LEAF:.*]] = obelisk_sim.managed.load %[[CONTAINER_LEAF_REF]]
// CHECK: obelisk_sim.managed.is_null %[[CONTAINER_LEAF]]
// CHECK: obelisk_sim.class.field_ref %[[CONTAINER_LEAF]][@__obelisk_class_s3_leaf_field___obelisk_rand_mode]
// CHECK: %[[DATA_REF:.*]] = obelisk_sim.class.field_ref %[[CONTAINER_LEAF]][@__obelisk_class_s3_leaf_field_1]
// CHECK: [[DATA]] = obelisk_sim.managed.load %[[DATA_REF]]
// CHECK: obelisk_sim.container.size [[DATA]]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[MIDDLE_REF]]
