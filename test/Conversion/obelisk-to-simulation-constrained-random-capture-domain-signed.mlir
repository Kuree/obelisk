// RUN: %if z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=CAPTURE-DOMAIN-SIGNED %}
// RUN: %if !z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=CAPTURE-DOMAIN-SIGNED-FALLBACK %}

// Signed captures and Z3-proven static domains use the same sign-bit-biased
// coordinates, so they can be intersected before sampling. Sampled values are
// un-biased before insertion into the aggregate assignment. Strict signed
// extrema take the same direct-failure edge as unsigned extrema.
// CAPTURE-DOMAIN-SIGNED-LABEL: simulation.func private @unit_1
// CAPTURE-DOMAIN-SIGNED-COUNT-2: arith.xori {{.*}}, %{{c8_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: %[[INCLUSIVE_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN-SIGNED: %[[LOW_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c15_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: arith.addi
// CAPTURE-DOMAIN-SIGNED: %[[HIGH_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: arith.subi
// CAPTURE-DOMAIN-SIGNED: arith.minui {{.*}}, %{{c14_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: %[[STRICT_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN-SIGNED: arith.andi
// CAPTURE-DOMAIN-SIGNED: cf.cond_br {{.*}}, ^[[SAMPLE:bb[0-9]+]], ^[[EMPTY:bb[0-9]+]]
// CAPTURE-DOMAIN-SIGNED: ^[[SAMPLE]]:
// CAPTURE-DOMAIN-SIGNED-COUNT-3: arith.xori {{.*}}, %{{c8_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: simulation.random.solve {{.*}} mutable

// CAPTURE-DOMAIN-SIGNED-FALLBACK-LABEL: simulation.func private @unit_1
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi sge
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi sle
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi sgt
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi slt
// CAPTURE-DOMAIN-SIGNED-FALLBACK: simulation.random.solve

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.symbol.variable @s15.low attributes {hierarchical_name = "low", lifetime = 1 : i32, name = "low", node_id = 26 : i64, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
      }
      obelisk.sv.symbol.variable @s16.high attributes {hierarchical_name = "high", lifetime = 1 : i32, name = "high", node_id = 27 : i64, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
      }
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 12 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property @s4.inclusive attributes {hierarchical_name = "C::inclusive", name = "inclusive", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
        }
        obelisk.sv.symbol.class_property @s17.strict attributes {hierarchical_name = "C::strict", name = "strict", node_id = 28 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
        }
        obelisk.sv.symbol.class_property @s18.combined attributes {hierarchical_name = "C::combined", name = "combined", node_id = 42 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
        }
        obelisk.sv.symbol.constraint_block @s5.bounded attributes {hierarchical_name = "C::bounded", name = "bounded", node_id = 5 : i64, this_variable_path = "C::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.bounded::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 6 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::inclusive", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.inclusive, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s15.low, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 29 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 30 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "C::inclusive", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.inclusive, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s16.high, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 33 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 34 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "C::strict", referenced_symbol = @s1.$root::@s2::@s3.C::@s17.strict, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s15.low, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 37 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 38 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "C::strict", referenced_symbol = @s1.$root::@s2::@s3.C::@s17.strict, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s16.high, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 43 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 44 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "C::combined", referenced_symbol = @s1.$root::@s2::@s3.C::@s18.combined, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 46 : i64, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 47 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 48 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "C::combined", referenced_symbol = @s1.$root::@s2::@s3.C::@s18.combined, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 50 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s16.high, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s6.this attributes {hierarchical_name = "C::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s7.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s9.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s10.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s11.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s12.top attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s13.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s14 attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}
