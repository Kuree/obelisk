// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | obelisk-opt --test-obelisk-print-random-programs -o /dev/null 2>&1 | FileCheck %s
// REQUIRES: compiled-sampler

// The sampler plan compiler prepares one decision diagram per set of random
// fields that an atom's top-level conjuncts read, so the target still sees
// which conjuncts read disjoint inputs and samples them apart (IEEE 1800-2023
// 18.5.9 permits any solving order with the same outcome). One diagram over
// interleaved independent fields would multiply their widths. A conjunct
// that the container size guards also records free fields: eliminating its
// element (or the size) alone leaves a true projection, so projecting onto
// the size layer (18.5.7.1 solves the size first) needs no work at run time.
//
//   class C;
//     rand bit [7:0] a[2];
//     rand bit [7:0] d[];
//     constraint c {
//       foreach (a[i]) a[i] < 100;
//       d.size() <= 2;
//       foreach (d[i]) d[i] < 100;
//     }
//   endclass
//   module top;
//     initial begin
//       static C c = new;
//       void'(c.randomize());
//     end
//   endmodule
//
// Bits 0-15 hold a[], 16-47 the size and 48-63 the shadow of d[].
// CHECK-LABEL: simulation.random.solve:
// Each element of a[] gets one diagram of its own.
// CHECK: plan root 9 conjuncts 2
// CHECK: plan root 13 conjuncts 1
// The guarded constraints on the elements of d[] share only the size, so
// each keeps its own diagram.
// CHECK: plan root 47 conjuncts 2
// CHECK-NEXT: plan free root 47 conjunct 0 offset 16 width 32
// CHECK-NEXT: plan free root 47 conjunct 0 offset 56 width 8
// CHECK-NEXT: plan free root 47 conjunct 1 offset 16 width 32
// CHECK-NEXT: plan free root 47 conjunct 1 offset 48 width 8

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s27.this} {
        obelisk.sv.symbol.class_property @s4.a attributes {hierarchical_name = "C::a", name = "a", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
        }
        obelisk.sv.symbol.class_property @s5.d attributes {hierarchical_name = "C::d", name = "d", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
        }
        obelisk.sv.symbol.constraint_block @s6.c attributes {hierarchical_name = "C::c", name = "c", node_id = 6 : i64, this_variable_path = "C::c.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s9.this} {
          obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.foreach attributes {loop_dimensions = [{has_iterator = true, has_static_range = true, iterator_path = "C::c.i", iterator_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s7.i, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>, left = 0 : i64, right = 1 : i64}], node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 10 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 11 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "C::c.i", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s7.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "32'd100", is_implicit = true, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "100", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 18 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 19 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "C::c", system_scope_symbol = @s1.$root::@s2::@s3.C::@s6.c} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "C::d", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.d, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.constraint.foreach attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "C::c.i", iterator_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s8.i, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}], node_id = 23 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "C::d", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.d, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 25 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 26 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "C::d", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.d, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 30 : i64, referenced_path = "C::c.i", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s8.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "32'd100", is_implicit = true, is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "100", is_declared_unsized = true, is_signed = true, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s9.this attributes {hierarchical_name = "C::c.this", is_compiler_generated, is_const, name = "this", node_id = 33 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
          obelisk.sv.symbol.iterator @s7.i attributes {array_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, hierarchical_name = "C::c.i", index_method_name = "", is_const, name = "i", node_id = 66 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.iterator @s8.i attributes {array_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, hierarchical_name = "C::c.i", index_method_name = "", is_const, name = "i", node_id = 67 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 34 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 35 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s11.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 36 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s12.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 38 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 39 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s13.get_randstate attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 40 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s14.set_randstate attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s15.state attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 44 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s16.srandom attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 45 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 46 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s17.seed attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s18.rand_mode attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 48 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 49 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s19.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 50 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s20.constraint_mode attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 51 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 52 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s21.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 53 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s27.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 68 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s22.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 54 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s23.top attributes {hierarchical_name = "top", name = "top", node_id = 55 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.statement_block @s24 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 56 : i64} {
          obelisk.sv.symbol.variable @s25.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 57 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 58 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s26 attributes {hierarchical_name = "top", node_id = 59 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 60 : i64} {
            obelisk.sv.statement.list attributes {node_id = 61 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 62 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s22.top::@s23.top::@s24::@s25.c} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s22.top::@s23.top::@s24} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s22.top::@s23.top::@s24::@s25.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
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

