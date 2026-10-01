// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.9: a reduction operator applies its logic table between
// the accumulated one-bit result and the next bit of the operand, producing a
// single-bit result.  Section 18.5.1 lets a constraint expression use one, so
// `rx == ^v` folds `v` bit by bit inside the randomize solver's constraint
// program.  Every value in that program below 65 bits is held as an i64, so
// each folded bit has to be widened into that representation before it is
// exclusive-ored into the accumulator.

module {
  obelisk.sv.symbol.definition @s0.t attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
      obelisk.sv.type.class_type @s3.C attributes {
        bitstream_width = 9 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 3 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
        this_variable_path = "C::this",
        this_variable_symbol = @s1.$root::@s2::@s3.C::@s25.this
      } {
        obelisk.sv.symbol.class_property @s4.v attributes {
          hierarchical_name = "C::v", name = "v", node_id = 4 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.class_property @s5.rx attributes {
          hierarchical_name = "C::rx", name = "rx", node_id = 5 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
        } {
        }
        obelisk.sv.symbol.constraint_block @s6.c attributes {
          hierarchical_name = "C::c", name = "c", node_id = 6 : i64,
          this_variable_path = "C::c.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s7.this
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 7 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = false, node_id = 8 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                is_signed = false, node_id = 9 : i64, operator_kind = 9 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 10 : i64,
                  referenced_path = "C::rx",
                  referenced_symbol = @s1.$root::@s2::@s3.C::@s5.rx,
                  semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                } {
                }
                obelisk.sv.expression.unary_op attributes {
                  is_signed = false, node_id = 11 : i64,
                  operator_kind = 5 : i32,
                  semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 12 : i64,
                    referenced_path = "C::v",
                    referenced_symbol = @s1.$root::@s2::@s3.C::@s4.v,
                    semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s7.this attributes {
            hierarchical_name = "C::c.this", is_compiler_generated, is_const,
            name = "this", node_id = 13 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
          } {
          }
        }
        obelisk.sv.symbol.variable @s25.this attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 46 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
        } {
        }
      }
    }
    obelisk.sv.symbol.instance @s20.t attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 34 : i64, referenced_path = "t", referenced_symbol = @s0.t
    } {
      obelisk.sv.symbol.instance_body @s21.t attributes {
        hierarchical_name = "t", name = "t", node_id = 35 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.statement_block @s22 attributes {
          block_kind = 0 : i32, hierarchical_name = "t", node_id = 36 : i64
        } {
          obelisk.sv.symbol.variable @s23.o attributes {
            hierarchical_name = "t.o", lifetime = 1 : i32, name = "o",
            node_id = 37 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
          } {
            obelisk.sv.expression.new_class attributes {
              is_signed = false, is_super_class = false, node_id = 38 : i64,
              semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
            } {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s24 attributes {
          hierarchical_name = "t", node_id = 39 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 40 : i64} {
            obelisk.sv.statement.list attributes {node_id = 41 : i64} {
              obelisk.sv.statement.variable_declaration attributes {
                node_id = 42 : i64, referenced_path = "t.o",
                referenced_symbol = @s1.$root::@s20.t::@s21.t::@s22::@s23.o
              } {
              }
              obelisk.sv.statement.expression_statement attributes {
                node_id = 43 : i64
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "randomize",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, is_void_casted, node_id = 44 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 0 : i32, system_library_cell = "work.t",
                  system_scope_path = "t",
                  system_scope_symbol = @s1.$root::@s20.t::@s21.t::@s22
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 45 : i64,
                    referenced_path = "t.o",
                    referenced_symbol = @s1.$root::@s20.t::@s21.t::@s22::@s23.o,
                    semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
                  } {
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



// CHECK-LABEL: simulation.design
// The accumulator starts at the operand's low bit and folds one bit per step.
// CHECK: %[[BIT0:.*]] = arith.cmpi ne, %{{.*}}, %{{.*}} : i64
// CHECK: %[[BIT1:.*]] = arith.cmpi ne, %{{.*}}, %{{.*}} : i64
// CHECK: %[[FOLD1:.*]] = arith.xori %[[BIT0]], %[[BIT1]] : i1
// CHECK: %[[BIT2:.*]] = arith.cmpi ne, %{{.*}}, %{{.*}} : i64
// CHECK: %[[FOLD2:.*]] = arith.xori %[[FOLD1]], %[[BIT2]] : i1
// The one-bit result rejoins the program's i64 value representation.
// CHECK: arith.extui %{{.*}} : i1 to i64
