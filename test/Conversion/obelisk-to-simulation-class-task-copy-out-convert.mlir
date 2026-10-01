// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 13.5: an output formal is assigned back to its actual when
// the subroutine returns, so a formal whose type differs from the actual --
// here an `int` formal taking a `byte` actual -- cannot share the actual's
// storage. The call allocates storage of the formal's own type and converts
// on the way back. A class method needs this as much as a plain task does.

module {
  obelisk.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.this} {
        obelisk.sv.symbol.subroutine @s4.get attributes {hierarchical_name = "C::get", name = "get", node_id = 4 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, this_variable_path = "C::get.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s4.get::@s6.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 5 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 7 : i64, referenced_path = "C::get.t", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.get::@s5.t, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s5.t attributes {direction = 1 : i32, hierarchical_name = "C::get.t", name = "t", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.variable @s6.this attributes {hierarchical_name = "C::get.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s8.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 12 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      obelisk.sv.symbol.instance_body @s9.m attributes {hierarchical_name = "m", name = "m", node_id = 13 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s10.c attributes {hierarchical_name = "m.c", lifetime = 1 : i32, name = "c", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
        obelisk.sv.symbol.variable @s11.b attributes {hierarchical_name = "m.b", lifetime = 1 : i32, name = "b", node_id = 15 : i64, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "m", node_id = 16 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 19 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "m.c", referenced_symbol = @s1.$root::@s8.m::@s9.m::@s10.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
                obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "get", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 23 : i64, referenced_path = "C::get", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.get, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "m.c", referenced_symbol = @s1.$root::@s8.m::@s9.m::@s10.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 26 : i64, referenced_path = "m.b", referenced_symbol = @s1.$root::@s8.m::@s9.m::@s11.b, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                    obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
}

// The call passes a reference of the formal's own type, and the value it
// leaves there is truncated into the actual on the continuation.
// CHECK: %[[FORMAL:.*]] = simulation.ref.alloc %{{.*}} : i32 -> !simulation.ref<i32>
// CHECK: simulation.task.call {{.*}}%[[FORMAL]]
// CHECK: ^bb{{[0-9]+}}(%[[COPY:.*]]: !simulation.ref<i32>):
// CHECK: %[[OUT:.*]] = simulation.ref.load %[[COPY]] : !simulation.ref<i32> -> i32
// CHECK: %[[TRUNC:.*]] = arith.trunci %[[OUT]] : i32 to i8
// CHECK: simulation.ref.store %[[TRUNC]] to %{{.*}} : i8, !simulation.ref<i8>
