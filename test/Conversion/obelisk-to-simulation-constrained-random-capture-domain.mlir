// RUN: %if z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=CAPTURE-DOMAIN %}
// RUN: %if !z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=CAPTURE-DOMAIN-FALLBACK %}

// Z3 proves the direct runtime bounds equivalent to the hard formula. The
// generated samplers compute cardinalities limit + 1, 16 - limit,
// high - low + 1, and high - low - 1. Strict endpoints are normalized only
// after checking their individual overflow edges. Any empty intersected range
// fails before modulo; valid ranges use unbiased dynamic sampling. The
// all-enabled path bypasses checking, while partial modes retain a masked
// solver path.
// CAPTURE-DOMAIN-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN: obelisk_sim.ref.load
// CAPTURE-DOMAIN: arith.andi {{.*}}, {{.*}} : i64
// CAPTURE-DOMAIN: %[[CARDINALITY:.*]] = arith.addi {{.*}}, {{.*}} : i64
// CAPTURE-DOMAIN: arith.subi %{{c16_i64.*}}, {{.*}} : i64
// CAPTURE-DOMAIN: %[[INCLUSIVE_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN: arith.subi
// CAPTURE-DOMAIN: arith.addi
// CAPTURE-DOMAIN: %[[LOW_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c15_i64.*}} : i64
// CAPTURE-DOMAIN: arith.andi %[[INCLUSIVE_VALID]], %[[LOW_EDGE]] : i1
// CAPTURE-DOMAIN: arith.addi
// CAPTURE-DOMAIN: arith.maxui
// CAPTURE-DOMAIN: %[[HIGH_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN: arith.andi {{.*}}, %[[HIGH_EDGE]] : i1
// CAPTURE-DOMAIN: arith.subi
// CAPTURE-DOMAIN: arith.minui
// CAPTURE-DOMAIN: %[[STRICT_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN: %[[ALL_VALID:.*]] = arith.andi {{.*}}, %[[STRICT_VALID]] : i1
// CAPTURE-DOMAIN: arith.subi
// CAPTURE-DOMAIN: arith.addi
// CAPTURE-DOMAIN: cf.cond_br %[[ALL_VALID]], ^[[RANGE_SAMPLE:bb[0-9]+]], ^[[RANGE_EMPTY:bb[0-9]+]]
// CAPTURE-DOMAIN: ^[[RANGE_SAMPLE]]:
// CAPTURE-DOMAIN: %[[FULL_CARDINALITY:.*]] = arith.cmpi eq, %[[SAMPLE_CARDINALITY:.*]], %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN: %[[SAFE_CARDINALITY:.*]] = arith.select %[[FULL_CARDINALITY]], %{{c1_i64.*}}, %[[SAMPLE_CARDINALITY]] : i64
// CAPTURE-DOMAIN: arith.remui {{.*}}, %[[SAFE_CARDINALITY]] : i64
// CAPTURE-DOMAIN: cf.br
// CAPTURE-DOMAIN: ^[[RANGE_EMPTY]]:
// CAPTURE-DOMAIN: obelisk_sim.managed.store {{.*}} : i64
// CAPTURE-DOMAIN: cf.br ^[[RANGE_DONE:bb[0-9]+]]
// CAPTURE-DOMAIN: arith.remui {{.*}}, %[[SAFE_CARDINALITY]] : i64
// CAPTURE-DOMAIN: arith.cmpi ult
// CAPTURE-DOMAIN-NOT: arith.cmpi ule
// CAPTURE-DOMAIN: obelisk_sim.random.solve {{.*}} mutable
// CAPTURE-DOMAIN: obelisk_sim.managed.store

// CAPTURE-DOMAIN-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi ule
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi uge
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi ugt
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi ult
// CAPTURE-DOMAIN-FALLBACK: obelisk_sim.random.solve

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "limit", lifetime = 1 : i32, name = "limit", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s15.limit"} {
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "low", lifetime = 1 : i32, name = "low", node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s17.low"} {
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "high", lifetime = 1 : i32, name = "high", node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s18.high"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::high", name = "high", node_id = 28 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s16.high"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::range", name = "range", node_id = 35 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s19.range"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::strict_range", name = "strict_range", node_id = 44 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s20.strict_range"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::bounded", name = "bounded", node_id = 5 : i64, sym_name = "s5.bounded", this_variable_path = "C::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.bounded::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 6 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s15.limit, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 29 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 30 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "C::high", referenced_symbol = @s1.$root::@s2::@s3.C::@s16.high, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s15.limit, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 36 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 37 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "C::range", referenced_symbol = @s1.$root::@s2::@s3.C::@s19.range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s17.low, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 40 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 41 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "C::range", referenced_symbol = @s1.$root::@s2::@s3.C::@s19.range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s18.high, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 45 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 46 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "C::strict_range", referenced_symbol = @s1.$root::@s2::@s3.C::@s20.strict_range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s17.low, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 49 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 50 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "C::strict_range", referenced_symbol = @s1.$root::@s2::@s3.C::@s20.strict_range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s18.high, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}
