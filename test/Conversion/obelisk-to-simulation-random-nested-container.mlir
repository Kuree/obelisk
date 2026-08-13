// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "child", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "child", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.child>, sym_name = "s3.child", this_variable_path = "child::this", this_variable_symbol = @s1.$root::@s2::@s3.child::@s39.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "child::data", name = "data", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>, sym_name = "s4.data"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "child::items", name = "items", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, sym_name = "s5.items"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "child::sized", name = "sized", node_id = 6 : i64, sym_name = "s6.sized", this_variable_path = "child::sized.this", this_variable_symbol = @s1.$root::@s2::@s3.child::@s6.sized::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 9 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "child::sized", system_scope_symbol = @s1.$root::@s2::@s3.child::@s6.sized} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "child::data", referenced_symbol = @s1.$root::@s2::@s3.child::@s4.data, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "child::sized.this", is_compiler_generated, is_const, name = "this", node_id = 13 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.child>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::pre_randomize", is_builtin, name = "pre_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::post_randomize", is_builtin, name = "post_randomize", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::get_randstate", is_builtin, name = "get_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s11.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::set_randstate", is_builtin, name = "set_randstate", node_id = 22 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 23 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "child::set_randstate.state", name = "state", node_id = 24 : i64, semantic_type = !obelisk.string, sym_name = "s13.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::srandom", is_builtin, name = "srandom", node_id = 25 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "child::srandom.seed", name = "seed", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::rand_mode", is_builtin, name = "rand_mode", node_id = 28 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s16.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "child::rand_mode.on_ff", name = "on_ff", node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s17.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "child::constraint_mode", is_builtin, name = "constraint_mode", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 32 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "child::constraint_mode.on_ff", name = "on_ff", node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s19.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "child::this", is_compiler_generated, is_const, name = "this", node_id = 64 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.child>, sym_name = "s39.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "parent", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "parent", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.parent>, sym_name = "s20.parent", this_variable_path = "parent::this", this_variable_symbol = @s1.$root::@s2::@s20.parent::@s38.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "parent::c", name = "c", node_id = 35 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.child>, sym_name = "s21.c"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 36 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s22.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::pre_randomize", is_builtin, name = "pre_randomize", node_id = 38 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s23.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 39 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::post_randomize", is_builtin, name = "post_randomize", node_id = 40 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::get_randstate", is_builtin, name = "get_randstate", node_id = 42 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s25.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::set_randstate", is_builtin, name = "set_randstate", node_id = 44 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 45 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "parent::set_randstate.state", name = "state", node_id = 46 : i64, semantic_type = !obelisk.string, sym_name = "s27.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::srandom", is_builtin, name = "srandom", node_id = 47 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 48 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "parent::srandom.seed", name = "seed", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s29.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::rand_mode", is_builtin, name = "rand_mode", node_id = 50 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s30.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 51 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "parent::rand_mode.on_ff", name = "on_ff", node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s31.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "parent::constraint_mode", is_builtin, name = "constraint_mode", node_id = 53 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s32.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 54 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "parent::constraint_mode.on_ff", name = "on_ff", node_id = 55 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s33.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "parent::this", is_compiler_generated, is_const, name = "this", node_id = 63 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.parent>, sym_name = "s38.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 56 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s34.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 57 : i64, sym_name = "s35.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 58 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.parent>, sym_name = "s36.p"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 59 : i64, procedure_kind = 0 : i32, sym_name = "s37", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s34.top::@s35.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s34.top::@s35.top::@s36.p, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.parent>} {
              }
            }
          }
        }
      }
    }
  }
}


// A nested rand container participates in the child's constraint solve. Its
// size is resized before element randomization. Both the constrained dynamic
// array and the unconstrained queue are gated by the outer rand handle, a
// non-null child, and their respective child rand_mode bits.
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// CHECK: %[[CHILD_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s20_parent_field_0]
// CHECK: %[[MODE_CHILD:.*]] = obelisk_sim.managed.load %[[CHILD_REF]]
// CHECK: obelisk_sim.class.field_ref %[[MODE_CHILD]][@__obelisk_class_s3_child_field___obelisk_constraint_mode]
// CHECK: ^{{bb[0-9]+}}({{.*}}):
// CHECK: %[[SIZE_CHILD:.*]] = obelisk_sim.managed.load %[[CHILD_REF]]
// CHECK: %[[SIZE_NULL:.*]] = obelisk_sim.managed.is_null %[[SIZE_CHILD]]
// CHECK: cf.cond_br %[[SIZE_NULL]], ^[[SIZE_MERGE:bb[0-9]+]]({{.*}}%false{{.*}}), ^[[SIZE_OBJECT:bb[0-9]+]]
// CHECK: ^[[SIZE_OBJECT]]:
// CHECK: %[[SIZE_REF:.*]] = obelisk_sim.class.field_ref %[[SIZE_CHILD]][@__obelisk_class_s3_child_field_0]
// CHECK: %[[OLD_ARRAY:.*]] = obelisk_sim.managed.load %[[SIZE_REF]]
// CHECK: obelisk_sim.class.field_ref %[[SIZE_CHILD]][@__obelisk_class_s3_child_field___obelisk_rand_mode]
// CHECK: obelisk_sim.container.size %[[OLD_ARRAY]]
// CHECK: ^[[SIZE_MERGE]]({{.*}}, %[[CHILD_SIZE_ENABLED:.*]]: i1):
// CHECK: arith.andi {{.*}}, %[[CHILD_SIZE_ENABLED]] : i1
// CHECK: %[[COMMIT_CHILD:.*]] = obelisk_sim.managed.load %[[CHILD_REF]]
// CHECK: %[[COMMIT_ARRAY_REF:.*]] = obelisk_sim.class.field_ref %[[COMMIT_CHILD]][@__obelisk_class_s3_child_field_0]
// CHECK: %[[COMMIT_OLD:.*]] = obelisk_sim.managed.load %[[COMMIT_ARRAY_REF]]
// CHECK: %[[RESIZED:.*]] = obelisk_sim.container.create_like %[[COMMIT_OLD]], %[[COMMIT_OLD]],
// CHECK: obelisk_sim.managed.store %[[RESIZED]] to %[[COMMIT_ARRAY_REF]]
// CHECK: %[[ARRAY_CHILD:.*]] = obelisk_sim.managed.load %[[CHILD_REF]]
// CHECK: %[[ARRAY_NULL:.*]] = obelisk_sim.managed.is_null %[[ARRAY_CHILD]]
// CHECK: cf.cond_br %[[ARRAY_NULL]], ^[[AFTER_ARRAY:bb[0-9]+]], ^[[ARRAY_MODE:bb[0-9]+]]
// CHECK: obelisk_sim.container.write [[ARRAY:%[^,]+]],
// CHECK: ^[[ARRAY_MODE]]:
// CHECK: %[[ARRAY_MODE_REF:.*]] = obelisk_sim.class.field_ref %[[ARRAY_CHILD]][@__obelisk_class_s3_child_field___obelisk_rand_mode]
// CHECK: %[[ARRAY_MODES:.*]] = obelisk_sim.managed.load %[[ARRAY_MODE_REF]]
// CHECK: arith.andi %[[ARRAY_MODES]], {{.*}}1
// CHECK: cf.cond_br {{.*}}, ^[[ARRAY_ACTIVE:bb[0-9]+]], ^[[AFTER_ARRAY]]
// CHECK: ^[[ARRAY_ACTIVE]]:
// CHECK: %[[ARRAY_REF:.*]] = obelisk_sim.class.field_ref %[[ARRAY_CHILD]][@__obelisk_class_s3_child_field_0]
// CHECK: [[ARRAY]] = obelisk_sim.managed.load %[[ARRAY_REF]]
// CHECK: %[[ARRAY_SIZE:.*]] = obelisk_sim.container.size [[ARRAY]]
// CHECK: %[[QUEUE_CHILD:.*]] = obelisk_sim.managed.load %[[CHILD_REF]]
// CHECK: %[[QUEUE_NULL:.*]] = obelisk_sim.managed.is_null %[[QUEUE_CHILD]]
// CHECK: cf.cond_br %[[QUEUE_NULL]], {{.*}}, ^[[QUEUE_MODE:bb[0-9]+]]
// CHECK: obelisk_sim.container.write [[QUEUE:%[^,]+]],
// CHECK: ^[[QUEUE_MODE]]:
// CHECK: %[[QUEUE_MODE_REF:.*]] = obelisk_sim.class.field_ref %[[QUEUE_CHILD]][@__obelisk_class_s3_child_field___obelisk_rand_mode]
// CHECK: %[[QUEUE_MODES:.*]] = obelisk_sim.managed.load %[[QUEUE_MODE_REF]]
// CHECK: arith.andi %[[QUEUE_MODES]], {{.*}}2
// CHECK: %[[QUEUE_REF:.*]] = obelisk_sim.class.field_ref %[[QUEUE_CHILD]][@__obelisk_class_s3_child_field_1]
// CHECK: [[QUEUE]] = obelisk_sim.managed.load %[[QUEUE_REF]]
// CHECK: obelisk_sim.container.size [[QUEUE]]
// CHECK-NOT: obelisk_sim.container.create_like [[QUEUE]]
