// RUN: %if z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SOLVE-BEFORE %}

// `select` has two legal values. After choosing it uniformly, `data` has two
// legal values when select is zero and one when select is one. The generated
// CFG therefore branches on one random bit at the first solve layer and draws
// a second random bit only down the two-row child. The all-enabled path commits
// the selected row directly; masked modes retain the runtime fallback.
// SOLVE-BEFORE-LABEL: obelisk_sim.func private @unit_1
// A compile-time solve table is valid only when every property and constraint
// block is enabled. Any partial rand_mode or constraint_mode mask branches to
// the residual ordered solver.
// SOLVE-BEFORE: %[[PROPERTY_MODE_REF:.*]] = obelisk_sim.class.field_ref {{.*}}__obelisk_rand_mode
// SOLVE-BEFORE: %[[CONSTRAINT_MODE_REF:.*]] = obelisk_sim.class.field_ref {{.*}}__obelisk_constraint_mode
// SOLVE-BEFORE: %[[PROPERTY_MODES:.*]] = obelisk_sim.managed.load %[[PROPERTY_MODE_REF]]
// SOLVE-BEFORE: %[[CONSTRAINT_MODES:.*]] = obelisk_sim.managed.load %[[CONSTRAINT_MODE_REF]]
// SOLVE-BEFORE: %[[RELEVANT_PROPERTY_MODES:.*]] = arith.andi %[[PROPERTY_MODES]], {{.*}} : i64
// SOLVE-BEFORE: %[[ALL_PROPERTIES_ENABLED:.*]] = arith.cmpi eq, %[[RELEVANT_PROPERTY_MODES]], {{.*}} : i64
// SOLVE-BEFORE: %[[RELEVANT_CONSTRAINT_MODES:.*]] = arith.andi %[[CONSTRAINT_MODES]], {{.*}} : i64
// SOLVE-BEFORE: %[[ALL_CONSTRAINTS_ENABLED:.*]] = arith.cmpi eq, %[[RELEVANT_CONSTRAINT_MODES]], {{.*}} : i64
// SOLVE-BEFORE: %[[USE_COMPILE_PLAN:.*]] = arith.andi %[[ALL_CONSTRAINTS_ENABLED]], %[[ALL_PROPERTIES_ENABLED]] : i1
// SOLVE-BEFORE: cf.cond_br %[[USE_COMPILE_PLAN]], ^[[COMPILE_PLAN:bb[0-9]+]],
// SOLVE-BEFORE: ^[[COMPILE_PLAN]]:
// SOLVE-BEFORE: %[[FIRST_INDEX:.*]] = arith.andi {{.*}}, {{.*}} : i64
// SOLVE-BEFORE: %[[FIRST_ZERO:.*]] = arith.cmpi eq, %[[FIRST_INDEX]], {{.*}} : i64
// SOLVE-BEFORE: cf.cond_br %[[FIRST_ZERO]], ^[[SECOND_DRAW:bb[0-9]+]],
// SOLVE-BEFORE: ^[[SECOND_DRAW]]:
// SOLVE-BEFORE: arith.muli
// SOLVE-BEFORE: %[[SECOND_INDEX:.*]] = arith.andi {{.*}}, {{.*}} : i64
// SOLVE-BEFORE: %[[SECOND_ZERO:.*]] = arith.cmpi eq, %[[SECOND_INDEX]], {{.*}} : i64
// SOLVE-BEFORE: cf.cond_br %[[SECOND_ZERO]]
// SOLVE-BEFORE: obelisk_sim.random.solve

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 300 : i64, sym_name = "sb0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 301 : i64, sym_name = "sb1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 302 : i64, sym_name = "sb2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 303 : i64, semantic_type = !obelisk.class_handle<@sb1.$root::@sb2::@sb3.C>, sym_name = "sb3.C", this_variable_path = "C::this", this_variable_symbol = @sb1.$root::@sb2::@sb3.C::@sb11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::select", name = "select", node_id = 304 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "sb4.select"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::data", name = "data", node_id = 305 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "sb5.data"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::rules", name = "rules", node_id = 306 : i64, sym_name = "sb6.rules", this_variable_path = "C::rules.this", this_variable_symbol = @sb1.$root::@sb2::@sb3.C::@sb6.rules::@sb7.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 307 : i64} {
            obelisk.sv.constraint.implication attributes {node_id = 308 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 309 : i64, referenced_path = "C::select", referenced_symbol = @sb1.$root::@sb2::@sb3.C::@sb4.select, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 310 : i64} {
                obelisk.sv.expression.binary_op attributes {node_id = 311 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.conversion attributes {node_id = 312 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 333 : i64, referenced_path = "C::data", referenced_symbol = @sb1.$root::@sb2::@sb3.C::@sb5.data, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 313 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 334 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 314 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 315 : i64, referenced_path = "C::select", referenced_symbol = @sb1.$root::@sb2::@sb3.C::@sb4.select, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 316 : i64, referenced_path = "C::data", referenced_symbol = @sb1.$root::@sb2::@sb3.C::@sb5.data, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::rules.this", is_compiler_generated, is_const, name = "this", node_id = 317 : i64, semantic_type = !obelisk.class_handle<@sb1.$root::@sb2::@sb3.C>, sym_name = "sb7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 318 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "sb8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 319 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 320 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "sb9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 321 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 322 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "sb10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 323 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 324 : i64, semantic_type = !obelisk.class_handle<@sb1.$root::@sb2::@sb3.C>, sym_name = "sb11.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 325 : i64, referenced_path = "top", referenced_symbol = @sb0.top, sym_name = "sb12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 326 : i64, sym_name = "sb13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 327 : i64, semantic_type = !obelisk.class_handle<@sb1.$root::@sb2::@sb3.C>, sym_name = "sb14.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 328 : i64, semantic_type = !obelisk.class_handle<@sb1.$root::@sb2::@sb3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 329 : i64, procedure_kind = 0 : i32, sym_name = "sb15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 330 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 331 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @sb1.$root::@sb12.top::@sb13.top} {
              obelisk.sv.expression.named_value attributes {node_id = 332 : i64, referenced_path = "top.object", referenced_symbol = @sb1.$root::@sb12.top::@sb13.top::@sb14.object, semantic_type = !obelisk.class_handle<@sb1.$root::@sb2::@sb3.C>} {
              }
            }
          }
        }
      }
    }
  }
}
