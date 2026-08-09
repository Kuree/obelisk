// RUN: %split-file %s %t
// RUN: obelisk-opt %t/success.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SUCCESS
// RUN: not obelisk-opt %t/cycle.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=CYCLE
// RUN: not obelisk-opt %t/output-formal.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk-opt %t/success.mlir '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t/success.mlir '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// The function body is expanded into both the generated checker and residual
// program. Its rand `bias` read remains a pre-solve capture, while the actual
// argument `y` remains a candidate variable. The implicit y-before-x edge sets
// the solve-before program flag (2) without a source solve-before constraint.
// SUCCESS-LABEL: obelisk_sim.func private @unit_1
// SUCCESS: obelisk_sim.managed.load
// SUCCESS: arith.addi
// SUCCESS: obelisk_sim.random.solve
// SUCCESS-SAME: captures(%{{.*}})
// SUCCESS-SAME: "ODR1\01\00\18\00\0A\00\00\00
// SUCCESS-SAME: \02\00\00\00
// The two path masks for s.b -> s.a are preserved instead of collapsing to
// the containing property: s starts at bit 6, so the masks are 0xc0 and 0x300.
// SUCCESS-SAME: \C0\00\00\00\00\00\00\00\00\03\00\00\00\00\00\00

// CYCLE: error: solve before ordering contains a cycle
// OUTPUT: error: constraint functions cannot have output, inout, or non-const ref arguments

//--- success.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 10 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s13.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {}
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.y"} {}
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::bias", name = "bias", node_id = 6 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s6.bias"} {}
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::s", name = "s", node_id = 300 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 4, 4, 4, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 2 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>, sym_name = "s40.s"} {}
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::map", name = "map", node_id = 7 : i64, return_variable_path = "C::map.map", return_variable_symbol = @s1.$root::@s2::@s3.C::@s7.map::@s9.map, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>) -> !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, false>, subroutine_kind = 0 : i32, sym_name = "s7.map", this_variable_path = "C::map.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.map::@s10.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 81 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 82 : i64, referenced_path = "C::map.map", referenced_symbol = @s1.$root::@s2::@s3.C::@s7.map::@s9.map, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 83 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 84 : i64, referenced_path = "C::map.value", referenced_symbol = @s1.$root::@s2::@s3.C::@s7.map::@s8.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 85 : i64, referenced_path = "C::bias", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.bias, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::map.value", name = "value", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s8.value"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::map.map", is_compiler_generated, name = "map", node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s9.map"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::map.this", is_compiler_generated, is_const, name = "this", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {}
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::rules", name = "rules", node_id = 15 : i64, sym_name = "s11.rules", this_variable_path = "C::rules.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s11.rules::@s12.this} {
          obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 16 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 17 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 18 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "map", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 20 : i64, referenced_path = "C::map", referenced_symbol = @s1.$root::@s2::@s3.C::@s7.map, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 301 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 302 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 315 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, is_signed = false, node_id = 303 : i64, packed_offset = 2 : i64, referenced_path = ".a", referenced_symbol = @s1.$root::@s2::@s50.Pair::@s51.a, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 304 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s40.s, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 4, 4, 4, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 2 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>} {}
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 311 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 312 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "map", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 305 : i64, referenced_path = "C::map", referenced_symbol = @s1.$root::@s2::@s3.C::@s7.map, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 316 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, is_signed = false, node_id = 306 : i64, packed_offset = 0 : i64, referenced_path = ".b", referenced_symbol = @s1.$root::@s2::@s50.Pair::@s52.b, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 307 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s40.s, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 4, 4, 4, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 2 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>} {}
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 313 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 314 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 317 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 318 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 319 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 320 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s40.s, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 4, 4, 4, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 2 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 321 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 322 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::rules.this", is_compiler_generated, is_const, name = "this", node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.this"} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.this"} {}
      }
      obelisk.sv.type.packed_struct_type attributes {hierarchical_name = "$unit", node_id = 308 : i64, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 4, 4, 4, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 2 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>, sym_name = "s50.Pair"} {
        obelisk.sv.symbol.field attributes {bit_offset = 2 : i64, field_index = 0 : i64, hierarchical_name = ".a", name = "a", node_id = 309 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s51.a"} {}
        obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 1 : i64, hierarchical_name = ".b", name = "b", node_id = 310 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s52.b"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 24 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s14.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 25 : i64, sym_name = "s15.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s16.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 27 : i64, procedure_kind = 0 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s14.top::@s15.top} {
              obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s14.top::@s15.top::@s16.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
            }
          }
        }
      }
    }
  }
}

//--- cycle.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100 : i64, sym_name = "c0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 101 : i64, sym_name = "c1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 102 : i64, sym_name = "c2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 103 : i64, semantic_type = !obelisk.class_handle<@c1.$root::@c2::@c3.C>, sym_name = "c3.C", this_variable_path = "C::this", this_variable_symbol = @c1.$root::@c2::@c3.C::@c13.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 104 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "c4.x"} {}
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 105 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "c5.y"} {}
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::id", name = "id", node_id = 106 : i64, return_variable_path = "C::id.id", return_variable_symbol = @c1.$root::@c2::@c3.C::@c7.id::@c9.id, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, sym_name = "c7.id", this_variable_path = "C::id.this", this_variable_symbol = @c1.$root::@c2::@c3.C::@c7.id::@c10.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.return attributes {node_id = 107 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 108 : i64, referenced_path = "C::id.value", referenced_symbol = @c1.$root::@c2::@c3.C::@c7.id::@c8.value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::id.value", name = "value", node_id = 109 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "c8.value"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::id.id", is_compiler_generated, name = "id", node_id = 110 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "c9.id"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::id.this", is_compiler_generated, is_const, name = "this", node_id = 111 : i64, semantic_type = !obelisk.class_handle<@c1.$root::@c2::@c3.C>, sym_name = "c10.this"} {}
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::rules", name = "rules", node_id = 112 : i64, sym_name = "c11.rules", this_variable_path = "C::rules.this", this_variable_symbol = @c1.$root::@c2::@c3.C::@c11.rules::@c12.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 113 : i64} {
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 114 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "C::x", referenced_symbol = @c1.$root::@c2::@c3.C::@c4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
              obelisk.sv.expression.named_value attributes {node_id = 116 : i64, referenced_path = "C::y", referenced_symbol = @c1.$root::@c2::@c3.C::@c5.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 117 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 118 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 119 : i64, referenced_path = "C::x", referenced_symbol = @c1.$root::@c2::@c3.C::@c4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 120 : i64, referenced_path = "C::id", referenced_symbol = @c1.$root::@c2::@c3.C::@c7.id, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 121 : i64, referenced_path = "C::y", referenced_symbol = @c1.$root::@c2::@c3.C::@c5.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::rules.this", is_compiler_generated, is_const, name = "this", node_id = 122 : i64, semantic_type = !obelisk.class_handle<@c1.$root::@c2::@c3.C>, sym_name = "c12.this"} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 123 : i64, semantic_type = !obelisk.class_handle<@c1.$root::@c2::@c3.C>, sym_name = "c13.this"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 124 : i64, referenced_path = "top", referenced_symbol = @c0.top, sym_name = "c14.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 125 : i64, sym_name = "c15.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 126 : i64, semantic_type = !obelisk.class_handle<@c1.$root::@c2::@c3.C>, sym_name = "c16.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 127 : i64, procedure_kind = 0 : i32, sym_name = "c17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 128 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 129 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @c1.$root::@c14.top::@c15.top} {
              obelisk.sv.expression.named_value attributes {node_id = 130 : i64, referenced_path = "top.object", referenced_symbol = @c1.$root::@c14.top::@c15.top::@c16.object, semantic_type = !obelisk.class_handle<@c1.$root::@c2::@c3.C>} {}
            }
          }
        }
      }
    }
  }
}

//--- output-formal.mlir

// Reuse the cycle fixture, changing only the formal direction and omitting the
// explicit solve edge so the purity-contract diagnostic is the first failure.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 200 : i64, sym_name = "o0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 201 : i64, sym_name = "o1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 202 : i64, sym_name = "o2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 1 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 203 : i64, semantic_type = !obelisk.class_handle<@o1.$root::@o2::@o3.C>, sym_name = "o3.C", this_variable_path = "C::this", this_variable_symbol = @o1.$root::@o2::@o3.C::@o11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 204 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "o4.x"} {}
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::bad", name = "bad", node_id = 205 : i64, return_variable_path = "C::bad.bad", return_variable_symbol = @o1.$root::@o2::@o3.C::@o5.bad::@o7.bad, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, sym_name = "o5.bad", this_variable_path = "C::bad.this", this_variable_symbol = @o1.$root::@o2::@o3.C::@o5.bad::@o8.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.return attributes {node_id = 206 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 207 : i64, referenced_path = "C::bad.value", referenced_symbol = @o1.$root::@o2::@o3.C::@o5.bad::@o6.value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "C::bad.value", name = "value", node_id = 208 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "o6.value"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bad.bad", is_compiler_generated, name = "bad", node_id = 209 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "o7.bad"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bad.this", is_compiler_generated, is_const, name = "this", node_id = 210 : i64, semantic_type = !obelisk.class_handle<@o1.$root::@o2::@o3.C>, sym_name = "o8.this"} {}
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::rules", name = "rules", node_id = 211 : i64, sym_name = "o9.rules", this_variable_path = "C::rules.this", this_variable_symbol = @o1.$root::@o2::@o3.C::@o9.rules::@o10.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 212 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 213 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "bad", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 214 : i64, referenced_path = "C::bad", referenced_symbol = @o1.$root::@o2::@o3.C::@o5.bad, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.named_value attributes {node_id = 215 : i64, referenced_path = "C::x", referenced_symbol = @o1.$root::@o2::@o3.C::@o4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::rules.this", is_compiler_generated, is_const, name = "this", node_id = 216 : i64, semantic_type = !obelisk.class_handle<@o1.$root::@o2::@o3.C>, sym_name = "o10.this"} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 217 : i64, semantic_type = !obelisk.class_handle<@o1.$root::@o2::@o3.C>, sym_name = "o11.this"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 218 : i64, referenced_path = "top", referenced_symbol = @o0.top, sym_name = "o12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 219 : i64, sym_name = "o13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 220 : i64, semantic_type = !obelisk.class_handle<@o1.$root::@o2::@o3.C>, sym_name = "o14.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 221 : i64, procedure_kind = 0 : i32, sym_name = "o15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 222 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 223 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @o1.$root::@o12.top::@o13.top} {
              obelisk.sv.expression.named_value attributes {node_id = 224 : i64, referenced_path = "top.object", referenced_symbol = @o1.$root::@o12.top::@o13.top::@o14.object, semantic_type = !obelisk.class_handle<@o1.$root::@o2::@o3.C>} {}
            }
          }
        }
      }
    }
  }
}
