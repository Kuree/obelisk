// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// IEEE 1800-2017 6.19.5 defines first(), last(), next(), prev(), num(), and
// name(). Iteration follows declaration order and wraps; an invalid next() or
// prev() receiver returns the enum base type's default initial value. name()
// returns the exact enumerator spelling and the empty string for invalid values.
// Four-state enum members containing x or z are legal, so membership must use
// exact four-state equality.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[COUNT:.*]] = arith.constant 3 : i32
// CHECK-DAG: %[[BLUE_VALUE:.*]] = obelisk_sim.logic.constant -7 : i4, 0 : i4
// CHECK-DAG: %[[X_VALUE:.*]] = obelisk_sim.logic.constant 0 : i4, -1 : i4
// CHECK-DAG: %[[RED_VALUE:.*]] = obelisk_sim.logic.constant 1 : i4, 0 : i4
// CHECK: %[[EMPTY_LOGIC:.*]] = obelisk_sim.string.literal ""
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: %[[RED:.*]] = obelisk_sim.string.literal "RED"
// CHECK: arith.select {{.*}}, %[[RED]], %[[EMPTY_LOGIC]] : !obelisk_sim.string
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: %[[XVAL:.*]] = obelisk_sim.string.literal "XVAL"
// CHECK: arith.select {{.*}}, %[[XVAL]], {{.*}} : !obelisk_sim.string
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: %[[BLUE:.*]] = obelisk_sim.string.literal "BLUE"
// CHECK: arith.select {{.*}}, %[[BLUE]], {{.*}} : !obelisk_sim.string
// CHECK: %[[EMPTY_INT:.*]] = obelisk_sim.string.literal ""
// CHECK: arith.cmpi eq
// CHECK: %[[START:.*]] = obelisk_sim.string.literal "START"
// CHECK: arith.select {{.*}}, %[[START]], %[[EMPTY_INT]] : !obelisk_sim.string
// CHECK: arith.cmpi eq
// CHECK: %[[STOP:.*]] = obelisk_sim.string.literal "STOP"
// CHECK: arith.select {{.*}}, %[[STOP]], {{.*}} : !obelisk_sim.string
// CHECK: obelisk_sim.ref.store %[[RED_VALUE]] to {{.*}}
// CHECK: obelisk_sim.ref.store %[[BLUE_VALUE]] to {{.*}}
// CHECK: arith.extui {{.*}} : i32 to i64
// CHECK: arith.remui
// CHECK: arith.addi
// CHECK: arith.remui
// CHECK: arith.select {{.*}}, {{.*}}, %[[X_VALUE]] : !obelisk_sim.logic<4>
// CHECK: obelisk_sim.ref.store {{.*}} to {{.*}}
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: arith.addi
// CHECK: arith.remui
// CHECK: arith.select {{.*}}, {{.*}}, %[[X_VALUE]] : !obelisk_sim.logic<4>
// CHECK: obelisk_sim.ref.store {{.*}} to {{.*}}
// CHECK: obelisk_sim.ref.store %[[COUNT]] to {{.*}}

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64, sym_name = "s0.top"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {}
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top, sym_name = "s3.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64,
        sym_name = "s4.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.logic_color", lifetime = 1 : i32,
          name = "logic_color", node_id = 5 : i64,
          semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>,
          sym_name = "s5.logic_color"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.logic_name", lifetime = 1 : i32,
          name = "logic_name", node_id = 6 : i64,
          semantic_type = !obelisk.string, sym_name = "s6.logic_name"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.int_state", lifetime = 1 : i32,
          name = "int_state", node_id = 7 : i64,
          semantic_type = !obelisk.enum<"top.int_state_t", !obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s7.int_state"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.int_name", lifetime = 1 : i32,
          name = "int_name", node_id = 8 : i64,
          semantic_type = !obelisk.string, sym_name = "s8.int_name"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.count_arg", lifetime = 1 : i32,
          name = "count_arg", node_id = 21 : i64,
          semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>,
          sym_name = "s21.count_arg"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.count_result", lifetime = 1 : i32,
          name = "count_result", node_id = 22 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s22.count_result"
        } {}
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top", node_id = 9 : i64,
          procedure_kind = 0 : i32, sym_name = "s9",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.expression_statement attributes {
              node_id = 11 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, node_id = 12 : i64,
                semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 13 : i64, referenced_path = "top.logic_name",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.logic_name,
                  semantic_type = !obelisk.string
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "name",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  enum_method_names = ["RED", "XVAL", "BLUE"],
                  enum_method_values = ["4'b0001", "4'bxxxx", "4'b1001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_super_class = false, is_system_call = true,
                  node_id = 14 : i64, semantic_type = !obelisk.string,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 15 : i64, referenced_path = "top.logic_color",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                    semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 16 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, node_id = 17 : i64,
                semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 18 : i64, referenced_path = "top.int_name",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.int_name,
                  semantic_type = !obelisk.string
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "name",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  enum_method_names = ["START", "STOP"],
                  enum_method_values = ["1", "-3"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = false, is_super_class = false,
                  is_system_call = true, node_id = 19 : i64,
                  semantic_type = !obelisk.string,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 20 : i64,
                    referenced_path = "top.int_state",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.int_state,
                    semantic_type = !obelisk.enum<"top.int_state_t", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 30 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, node_id = 31 : i64,
                semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 32 : i64, referenced_path = "top.logic_color",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "first",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  enum_method_values = ["4'b0001", "4'bxxxx", "4'b1001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_super_class = false, is_system_call = true,
                  node_id = 33 : i64,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 34 : i64, referenced_path = "top.logic_color",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                    semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 35 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, node_id = 36 : i64,
                semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 37 : i64, referenced_path = "top.logic_color",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "last",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  enum_method_values = ["4'b0001", "4'bxxxx", "4'b1001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_super_class = false, is_system_call = true,
                  node_id = 38 : i64,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 39 : i64, referenced_path = "top.logic_color",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                    semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 40 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, node_id = 41 : i64,
                semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 42 : i64, referenced_path = "top.logic_color",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "next",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  enum_method_values = ["4'b0001", "4'bxxxx", "4'b1001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_super_class = false, is_system_call = true,
                  node_id = 43 : i64,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 44 : i64, referenced_path = "top.logic_color",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                    semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                  } {}
                  obelisk.sv.expression.named_value attributes {
                    node_id = 45 : i64, referenced_path = "top.count_arg",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s21.count_arg,
                    semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 46 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, node_id = 47 : i64,
                semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 48 : i64, referenced_path = "top.logic_color",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "prev",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  enum_method_values = ["4'b0001", "4'bxxxx", "4'b1001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_super_class = false, is_system_call = true,
                  node_id = 49 : i64,
                  semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 50 : i64, referenced_path = "top.logic_color",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                    semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 51 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 52 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 53 : i64,
                  referenced_path = "top.count_result",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s22.count_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64, callee_name = "num",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0>,
                  enum_method_values = ["4'b0001", "4'bxxxx", "4'b1001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 54 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 55 : i64, referenced_path = "top.logic_color",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.logic_color,
                    semantic_type = !obelisk.enum<"top.logic_color_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                  } {}
                }
              }
            }
          }
        }
      }
    }
  }
}
