// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 18.9: `constraint_mode` names the constraint block to apply
// the operation to, on the object handle in which that block is defined.  A
// method of the owning class may name the block on its own, and 8.11 makes the
// object of such a call the `this` the method was invoked on -- here the
// constructor's own handle, so `hi.constraint_mode(0)` deactivates block 1 of
// the object under construction.

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
        bitstream_width = 8 : i64, constructor_path = "C::new",
        constructor_symbol = @s1.$root::@s2::@s3.C::@s9.new,
        declared_interfaces = [], generic_parameter_paths = [],
        generic_parameter_symbols = [], has_base_constructor_call = false,
        has_cycles = false, hierarchical_name = "C",
        implemented_interfaces = [], is_abstract = false, is_final = false,
        is_interface = false, is_uninstantiated = false, name = "C",
        node_id = 3 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
        this_variable_path = "C::this",
        this_variable_symbol = @s1.$root::@s2::@s3.C::@s27.this
      } {
        obelisk.sv.symbol.class_property @s4.v attributes {
          hierarchical_name = "C::v", name = "v", node_id = 4 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.constraint_block @s5.lo attributes {
          hierarchical_name = "C::lo", name = "lo", node_id = 5 : i64,
          this_variable_path = "C::lo.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.lo::@s6.this
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 6 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = false, node_id = 7 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                is_signed = false, node_id = 8 : i64, operator_kind = 16 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.conversion attributes {
                  is_signed = false, node_id = 9 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 10 : i64,
                    referenced_path = "C::v",
                    referenced_symbol = @s1.$root::@s2::@s3.C::@s4.v,
                    semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                }
                obelisk.sv.expression.conversion attributes {
                  folded_constant = "32'd50", is_signed = false,
                  node_id = 11 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "50", is_declared_unsized = true,
                    is_signed = true, node_id = 12 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s6.this attributes {
            hierarchical_name = "C::lo.this", is_compiler_generated, is_const,
            name = "this", node_id = 13 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
          } {
          }
        }
        obelisk.sv.symbol.constraint_block @s7.hi attributes {
          hierarchical_name = "C::hi", name = "hi", node_id = 14 : i64,
          this_variable_path = "C::hi.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.hi::@s8.this
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 15 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = false, node_id = 16 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                is_signed = false, node_id = 17 : i64,
                operator_kind = 13 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.conversion attributes {
                  is_signed = false, node_id = 18 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 19 : i64,
                    referenced_path = "C::v",
                    referenced_symbol = @s1.$root::@s2::@s3.C::@s4.v,
                    semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                }
                obelisk.sv.expression.conversion attributes {
                  folded_constant = "32'd50", is_signed = false,
                  node_id = 20 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "50", is_declared_unsized = true,
                    is_signed = true, node_id = 21 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s8.this attributes {
            hierarchical_name = "C::hi.this", is_compiler_generated, is_const,
            name = "this", node_id = 22 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
          } {
          }
        }
        obelisk.sv.symbol.subroutine @s9.new attributes {
          hierarchical_name = "C::new", is_constructor, name = "new",
          node_id = 23 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          this_variable_path = "C::new.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s9.new::@s10.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 24 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64, callee_name = "constraint_mode",
              constraint_restrictions = [],
              defaulted_arguments = array<i64: 0, 0>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = false,
              is_signed = false, is_super_class = false, is_system_call = true,
              node_id = 25 : i64, semantic_type = !obelisk.void,
              subroutine_kind = 0 : i32, system_library_cell = "work.$unit",
              system_scope_path = "C::new",
              system_scope_symbol = @s1.$root::@s2::@s3.C::@s9.new
            } {
              obelisk.sv.expression.arbitrary_symbol attributes {
                is_signed = false, node_id = 26 : i64,
                referenced_path = "C::hi",
                referenced_symbol = @s1.$root::@s2::@s3.C::@s7.hi,
                semantic_type = !obelisk.void
              } {
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "0", is_declared_unsized = true,
                is_signed = true, node_id = 27 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
            }
          }
          obelisk.sv.symbol.variable @s10.this attributes {
            hierarchical_name = "C::new.this", is_compiler_generated, is_const,
            name = "this", node_id = 28 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
          } {
          }
        }
        obelisk.sv.symbol.variable @s27.this attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 58 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
        } {
        }
      }
    }
    obelisk.sv.symbol.instance @s23.t attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 49 : i64, referenced_path = "t", referenced_symbol = @s0.t
    } {
      obelisk.sv.symbol.instance_body @s24.t attributes {
        hierarchical_name = "t", name = "t", node_id = 50 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s25.o attributes {
          hierarchical_name = "t.o", lifetime = 1 : i32, name = "o",
          node_id = 51 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
        } {
        }
        obelisk.sv.symbol.procedural_block @s26 attributes {
          hierarchical_name = "t", node_id = 52 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 53 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 54 : i64,
              semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 55 : i64, referenced_path = "t.o",
                referenced_symbol = @s1.$root::@s23.t::@s24.t::@s25.o,
                semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
              } {
              }
              obelisk.sv.expression.new_class attributes {
                is_signed = false, is_super_class = false, node_id = 56 : i64,
                semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64, callee_name = "new",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = false, is_super_class = false,
                  is_system_call = false, node_id = 57 : i64,
                  referenced_path = "C::new",
                  referenced_symbol = @s1.$root::@s2::@s3.C::@s9.new,
                  semantic_type = !obelisk.void, subroutine_kind = 0 : i32
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



// CHECK: simulation.class.decl @[[CLASS:[A-Za-z0-9_.$]+]] id 1
// CHECK-SAME: simulation.constraint_mode_field = @[[MODE:[A-Za-z0-9_.$]+]]

// The constructor takes `this` as its formal, and the unqualified block name
// resolves against it.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK-SAME: %[[THIS:[a-z0-9]+]]: !simulation.class_handle<@[[CLASS]]>
// CHECK-SAME: simulation.hierarchical_name = "C::new"
// CHECK: %[[BIT:.*]] = arith.constant 2 : i64
// CHECK: %[[REF:.*]] = simulation.class.field_ref %[[THIS]][@[[MODE]]]
// CHECK: %[[OLD:.*]] = simulation.managed.load %[[REF]]
// CHECK: %[[NEW:.*]] = arith.ori %[[OLD]], %[[BIT]] : i64
// CHECK: simulation.managed.store %[[NEW]] to %[[REF]]
