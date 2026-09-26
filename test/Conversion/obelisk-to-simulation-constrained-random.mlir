// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// The object stream chooses the first assignment for a bounded generated
// sampler. Exhaustion invokes the compiler-serialized runtime program, and the
// rand property is stored only on a successful commit edge.
// CHECK: obelisk_sim.class.decl @[[CLASS:[A-Za-z0-9_.$]+]]
// CHECK-SAME: random_constraint_template = @[[TEMPLATE:[A-Za-z0-9_.$]+]]
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "value"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_state"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_increment"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rand_mode"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_constraint_mode"
// CHECK: obelisk_sim.random.constraint_template @[[TEMPLATE]] of @[[CLASS]]
// CHECK-SAME: constraint_blocks = [#obelisk_sim.random_constraint_block_reference<kind = object_block, index = 0 : i32>]
// CHECK-SAME: references = [#obelisk_sim.random_value_reference<kind = object_field, target = @{{[^,]+}}, low = 0, width = 32>, #obelisk_sim.random_value_reference<kind = storage, storage = 0 : i64, low = 0, width = 32>]
// CHECK: %[[TEMPLATE_VALUE:.*]] = obelisk_sim.random.constraint_value 0 : i32
// CHECK: %[[TEMPLATE_LIMIT:.*]] = obelisk_sim.random.constraint_value 1 : i32
// CHECK: %[[TEMPLATE_GT:.*]] = arith.cmpi sgt, %[[TEMPLATE_VALUE]], %[[TEMPLATE_LIMIT]] : i32
// CHECK: obelisk_sim.random.soft_constraint %[[TEMPLATE_GT]] block 0 priority 0
// CHECK: obelisk_sim.random.soft_constraint %{{.*}} block 0 priority 1
// CHECK: obelisk_sim.func private @unit_1({{.*}}%[[LIMIT_ARG:arg[0-9]+]]: !obelisk_sim.ref<i32>
// CHECK: obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_rand_mode]
// CHECK: %[[OLD_MODE:.*]] = obelisk_sim.managed.load
// CHECK: %[[DISABLED_MODE:.*]] = arith.ori %[[OLD_MODE]], %{{c1_i64.*}} : i64
// CHECK: obelisk_sim.managed.store %[[DISABLED_MODE]]
// CHECK: obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_rand_mode]
// CHECK: %[[MODE:.*]] = obelisk_sim.managed.load
// CHECK: %[[PROPERTY_MODE:.*]] = arith.andi %[[MODE]], %{{c1_i64.*}} : i64
// CHECK: %[[MODE_ENABLED:.*]] = arith.cmpi eq, %[[PROPERTY_MODE]], %{{c0_i64.*}} : i64
// CHECK: arith.extui %[[MODE_ENABLED]] : i1 to i32
// CHECK: obelisk_sim.ref.store {{.*}} to %[[LIMIT_ARG]]
// CHECK: obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_rand_mode]
// CHECK: obelisk_sim.managed.store %{{c0_i64.*}}
// CHECK: obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: obelisk_sim.managed.store %{{c-1_i64.*}}
// CHECK: obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: %[[BLOCK_MODES:.*]] = obelisk_sim.managed.load
// CHECK: %[[BLOCK_MODE:.*]] = arith.andi %[[BLOCK_MODES]], %{{c1_i64.*}} : i64
// CHECK: %[[BLOCK_ENABLED:.*]] = arith.cmpi eq, %[[BLOCK_MODE]], %{{c0_i64.*}} : i64
// CHECK: arith.extui %[[BLOCK_ENABLED]] : i1 to i32
// CHECK: obelisk_sim.ref.store {{.*}} to %[[LIMIT_ARG]]
// CHECK: obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: %[[OLD_BLOCK_MODES:.*]] = obelisk_sim.managed.load
// CHECK: %[[ENABLED_BLOCK_MODES:.*]] = arith.andi %[[OLD_BLOCK_MODES]], %{{c-2_i64.*}} : i64
// CHECK: obelisk_sim.managed.store %[[ENABLED_BLOCK_MODES]]
// CHECK: %[[LIVE_BLOCK_MODE_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@{{.*}}__obelisk_constraint_mode]
// CHECK: %[[LIVE_BLOCK_MODES:.*]] = obelisk_sim.managed.load %[[LIVE_BLOCK_MODE_REF]]
// CHECK: %[[RELEVANT_MODE:.*]] = arith.andi
// CHECK: %[[ALL_PROPERTIES_ENABLED:.*]] = arith.cmpi eq, %[[RELEVANT_MODE]], %{{c0_i64.*}} : i64
// CHECK: %[[ALL_DISABLED:.*]] = arith.cmpi eq, %[[RELEVANT_MODE]], %{{c1_i64.*}} : i64
// CHECK: %[[RELEVANT_BLOCK_MODES:.*]] = arith.andi %[[LIVE_BLOCK_MODES]], %{{c1_i64.*}} : i64
// CHECK: %[[ALL_BLOCKS_ENABLED:.*]] = arith.cmpi eq, %[[RELEVANT_BLOCK_MODES]], %{{c0_i64.*}} : i64
// CHECK: arith.select
// CHECK: cf.cond_br %[[ALL_DISABLED]]
// CHECK: arith.muli
// CHECK: %[[USE_PLAN:.*]] = arith.andi %[[ALL_BLOCKS_ENABLED]], %[[ALL_PROPERTIES_ENABLED]] : i1
// CHECK: cf.cond_br %[[USE_PLAN]]
// CHECK: obelisk_sim.managed.store
// CHECK: arith.cmpi slt
// CHECK: arith.ori
// CHECK: arith.cmpi sgt
// CHECK: arith.select
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi sge
// CHECK: arith.cmpi sle
// CHECK: obelisk_sim.ref.load %[[LIMIT_ARG]]
// CHECK: cf.cond_br
// CHECK: arith.cmpi uge
// CHECK: obelisk_sim.random.solve {{.*}} mutable {{.*}} constraints %[[RELEVANT_BLOCK_MODES]]
// CHECK: cf.cond_br
// CHECK: arith.trunci
// CHECK: cf.cond_br
// CHECK: obelisk_sim.managed.store
// CHECK-NOT: obelisk.sv.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_constraint", name = "unsupported_constraint", node_id = 0 : i64, sym_name = "s0.unsupported_constraint"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "limit", lifetime = 1 : i32, name = "limit", node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.limit"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "constrained", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "constrained", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s3.constrained", this_variable_path = "constrained::this", this_variable_symbol = @s1.$root::@s2::@s3.constrained::@s21.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "constrained::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "constrained::bounds", name = "bounds", node_id = 5 : i64, sym_name = "s5.bounds", this_variable_path = "constrained::bounds.this", this_variable_symbol = @s1.$root::@s2::@s3.constrained::@s5.bounds::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s24.limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 230 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 231 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "constrained::bounds.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "constrained::this", is_compiler_generated, is_const, name = "this", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s21.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_constraint", is_uninstantiated = false, name = "unsupported_constraint", node_id = 32 : i64, referenced_path = "unsupported_constraint", referenced_symbol = @s0.unsupported_constraint, sym_name = "s19.unsupported_constraint"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_constraint", name = "unsupported_constraint", node_id = 33 : i64, sym_name = "s20.unsupported_constraint"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_constraint.object", lifetime = 1 : i32, name = "object", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s22.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_constraint", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 200 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 201 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_constraint", system_scope_path = "unsupported_constraint", system_scope_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint} {
              obelisk.sv.expression.member_access attributes {node_id = 202 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 213 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
                }
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 203 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 204 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 205 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 206 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s24.limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 211 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_constraint", system_scope_path = "unsupported_constraint", system_scope_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint} {
                obelisk.sv.expression.member_access attributes {node_id = 212 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 214 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
                  }
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 207 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 208 : i64, referenced_path = "constrained::rand_mode", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s15.rand_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.named_value attributes {node_id = 209 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 210 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 215 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 216 : i64, referenced_path = "constrained::constraint_mode", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s17.constraint_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.named_value attributes {node_id = 217 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 218 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 219 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 220 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 221 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s24.limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 222 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_constraint", system_scope_path = "unsupported_constraint", system_scope_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint} {
                obelisk.sv.expression.member_access attributes {node_id = 223 : i64, referenced_path = "constrained::bounds", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s5.bounds, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.named_value attributes {node_id = 224 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
                  }
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 225 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 226 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_constraint", system_scope_path = "unsupported_constraint", system_scope_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint} {
              obelisk.sv.expression.member_access attributes {node_id = 227 : i64, referenced_path = "constrained::bounds", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s5.bounds, semantic_type = !obelisk.void} {
                obelisk.sv.expression.named_value attributes {node_id = 228 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
                }
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 229 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_constraint", system_scope_path = "unsupported_constraint", system_scope_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint} {
              obelisk.sv.constraint.list attributes {item_count = 5 : i64, node_id = 41 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 42 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 43 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.constraint.implication attributes {node_id = 46 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 47 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 50 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 51 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.constraint.conditional attributes {has_else = true, node_id = 54 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 55 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 58 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 59 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 62 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 63 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 64 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 65 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.constraint.uniqueness attributes {item_count = 2 : i64, node_id = 66 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 67 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 69 : i64} {
                  obelisk.sv.expression.inside attributes {item_count = 1 : i64, node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 71 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.value_range attributes {node_id = 72 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 73 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 74 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
              }
            }
          }
        }
      }
    }
  }
}
