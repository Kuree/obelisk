// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.cast_fixture attributes {
    definition_kind = 0 : i32, hierarchical_name = "cast_fixture",
    name = "cast_fixture", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
      obelisk.sv.type.class_type @s100.A attributes {
        bitstream_width = 0 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "A", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "A", node_id = 100 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>,
        this_variable_path = "A::this",
        this_variable_symbol = @s1.$root::@s2::@s100.A::@s101.this
      } {
        obelisk.sv.symbol.variable @s101.this attributes {
          hierarchical_name = "A::this", is_compiler_generated, is_const,
          name = "this", node_id = 101 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
        } {}
      }
      obelisk.sv.type.class_type @s102.B attributes {
        bitstream_width = 0 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "B", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "B", node_id = 102 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s102.B>,
        this_variable_path = "B::this",
        this_variable_symbol = @s1.$root::@s2::@s102.B::@s103.this
      } {
        obelisk.sv.symbol.variable @s103.this attributes {
          hierarchical_name = "B::this", is_compiler_generated, is_const,
          name = "this", node_id = 103 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s102.B>
        } {}
      }
      obelisk.sv.type.class_type @s130.D attributes {
        base_class = !obelisk.class_handle<@s1.$root::@s2::@s100.A>,
        bitstream_width = 0 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "D", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "D", node_id = 130 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s130.D>,
        this_variable_path = "D::this",
        this_variable_symbol = @s1.$root::@s2::@s130.D::@s131.this
      } {
        obelisk.sv.symbol.variable @s131.this attributes {
          hierarchical_name = "D::this", is_compiler_generated, is_const,
          name = "this", node_id = 131 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s130.D>
        } {}
      }
    }
    obelisk.sv.symbol.instance @s3.cast_fixture attributes {
      hierarchical_name = "cast_fixture", is_uninstantiated = false,
      name = "cast_fixture", node_id = 3 : i64,
      referenced_path = "cast_fixture", referenced_symbol = @s0.cast_fixture
    } {
      obelisk.sv.symbol.instance_body @s4.cast_fixture attributes {
        hierarchical_name = "cast_fixture", name = "cast_fixture",
        node_id = 4 : i64,
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s5.enum_destination attributes {
          hierarchical_name = "cast_fixture.enum_destination",
          lifetime = 1 : i32, name = "enum_destination", node_id = 5 : i64,
          semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
        } {}
        obelisk.sv.symbol.variable @s6.integer_source attributes {
          hierarchical_name = "cast_fixture.integer_source",
          lifetime = 1 : i32, name = "integer_source", node_id = 6 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s7.enum_result attributes {
          hierarchical_name = "cast_fixture.enum_result",
          lifetime = 1 : i32, name = "enum_result", node_id = 7 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s8.integer_destination attributes {
          hierarchical_name = "cast_fixture.integer_destination",
          lifetime = 1 : i32, name = "integer_destination", node_id = 8 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s9.real_source attributes {
          hierarchical_name = "cast_fixture.real_source",
          lifetime = 1 : i32, name = "real_source", node_id = 9 : i64,
          semantic_type = !obelisk.real
        } {}
        obelisk.sv.symbol.variable @s10.value_result attributes {
          hierarchical_name = "cast_fixture.value_result",
          lifetime = 1 : i32, name = "value_result", node_id = 10 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s11.logic_destination attributes {
          hierarchical_name = "cast_fixture.logic_destination",
          lifetime = 1 : i32, name = "logic_destination", node_id = 11 : i64,
          semantic_type = !obelisk.enum<"L", !obelisk.integral<4, false, true, 3 : 0, logic>>
        } {}
        obelisk.sv.symbol.variable @s12.logic_source attributes {
          hierarchical_name = "cast_fixture.logic_source",
          lifetime = 1 : i32, name = "logic_source", node_id = 12 : i64,
          semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.variable @s13.logic_result attributes {
          hierarchical_name = "cast_fixture.logic_result",
          lifetime = 1 : i32, name = "logic_result", node_id = 13 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s120.narrow_destination attributes {
          hierarchical_name = "cast_fixture.narrow_destination",
          lifetime = 1 : i32, name = "narrow_destination",
          node_id = 120 : i64,
          semantic_type = !obelisk.enum<"N", !obelisk.integral<4, false, false, 3 : 0, bit>>
        } {}
        obelisk.sv.symbol.variable @s121.narrow_result attributes {
          hierarchical_name = "cast_fixture.narrow_result",
          lifetime = 1 : i32, name = "narrow_result", node_id = 121 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s104.class_destination attributes {
          hierarchical_name = "cast_fixture.class_destination",
          lifetime = 1 : i32, name = "class_destination",
          node_id = 104 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
        } {}
        obelisk.sv.symbol.variable @s105.class_source attributes {
          hierarchical_name = "cast_fixture.class_source",
          lifetime = 1 : i32, name = "class_source", node_id = 105 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s102.B>
        } {}
        obelisk.sv.symbol.variable @s106.class_result attributes {
          hierarchical_name = "cast_fixture.class_result",
          lifetime = 1 : i32, name = "class_result", node_id = 106 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {}
        obelisk.sv.symbol.variable @s132.derived_destination attributes {
          hierarchical_name = "cast_fixture.derived_destination",
          lifetime = 1 : i32, name = "derived_destination",
          node_id = 132 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s130.D>
        } {}
        obelisk.sv.symbol.procedural_block @s14 attributes {
          hierarchical_name = "cast_fixture", node_id = 14 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 15 : i64} {
            obelisk.sv.statement.expression_statement attributes {
              node_id = 16 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 17 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 18 : i64,
                  referenced_path = "cast_fixture.enum_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s7.enum_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 2 : i32,
                  dynamic_cast_enum_values = ["1", "4", "9"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 19 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 20 : i64,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 21 : i64,
                      referenced_path = "cast_fixture.enum_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s5.enum_destination,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = true, node_id = 22 : i64,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 23 : i64,
                    referenced_path = "cast_fixture.integer_source",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s6.integer_source,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 129 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 122 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 123 : i64,
                  referenced_path = "cast_fixture.narrow_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s121.narrow_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 2 : i32,
                  dynamic_cast_enum_values = ["4'b0001"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 124 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 125 : i64,
                    semantic_type = !obelisk.enum<"N", !obelisk.integral<4, false, false, 3 : 0, bit>>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 126 : i64,
                      referenced_path = "cast_fixture.narrow_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s120.narrow_destination,
                      semantic_type = !obelisk.enum<"N", !obelisk.integral<4, false, false, 3 : 0, bit>>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = false, node_id = 127 : i64,
                      semantic_type = !obelisk.enum<"N", !obelisk.integral<4, false, false, 3 : 0, bit>>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 128 : i64,
                    referenced_path = "cast_fixture.integer_source",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s6.integer_source,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 24 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 25 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 26 : i64,
                  referenced_path = "cast_fixture.value_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s10.value_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 0 : i32,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 27 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 28 : i64,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 29 : i64,
                      referenced_path = "cast_fixture.enum_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s5.enum_destination,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = true, node_id = 30 : i64,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 31 : i64,
                    referenced_path = "cast_fixture.real_source",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s9.real_source,
                    semantic_type = !obelisk.real
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 32 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 33 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 34 : i64,
                  referenced_path = "cast_fixture.logic_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s13.logic_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 2 : i32,
                  dynamic_cast_enum_values = ["4'b0001", "4'bx"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 35 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 36 : i64,
                    semantic_type = !obelisk.enum<"L", !obelisk.integral<4, false, true, 3 : 0, logic>>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 37 : i64,
                      referenced_path = "cast_fixture.logic_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s11.logic_destination,
                      semantic_type = !obelisk.enum<"L", !obelisk.integral<4, false, true, 3 : 0, logic>>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = false, node_id = 38 : i64,
                      semantic_type = !obelisk.enum<"L", !obelisk.integral<4, false, true, 3 : 0, logic>>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 39 : i64,
                    referenced_path = "cast_fixture.logic_source",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s12.logic_source,
                    semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 40 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 41 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 42 : i64,
                  referenced_path = "cast_fixture.value_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s10.value_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 2 : i32,
                  dynamic_cast_enum_values = ["1", "4", "9"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 43 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 44 : i64,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 45 : i64,
                      referenced_path = "cast_fixture.enum_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s5.enum_destination,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = true, node_id = 46 : i64,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 47 : i64,
                    referenced_path = "cast_fixture.enum_destination",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s5.enum_destination,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 48 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 49 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 50 : i64,
                  referenced_path = "cast_fixture.value_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s10.value_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 2 : i32,
                  dynamic_cast_enum_values = ["1", "4", "9"],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 51 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 52 : i64,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 53 : i64,
                      referenced_path = "cast_fixture.enum_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s5.enum_destination,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = true, node_id = 54 : i64,
                      semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 55 : i64,
                    referenced_path = "cast_fixture.logic_destination",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s11.logic_destination,
                    semantic_type = !obelisk.enum<"L", !obelisk.integral<4, false, true, 3 : 0, logic>>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 147 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 133 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 134 : i64,
                  referenced_path = "cast_fixture.class_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s106.class_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 3 : i32,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 135 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 136 : i64,
                    semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s130.D>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 137 : i64,
                      referenced_path = "cast_fixture.derived_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s132.derived_destination,
                      semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s130.D>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = false, node_id = 138 : i64,
                      semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s130.D>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 139 : i64,
                    referenced_path = "cast_fixture.class_destination",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s104.class_destination,
                    semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 148 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 140 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 141 : i64,
                  referenced_path = "cast_fixture.class_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s106.class_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 0 : i32,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 142 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 143 : i64,
                    semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 144 : i64,
                      referenced_path = "cast_fixture.class_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s104.class_destination,
                      semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = false, node_id = 145 : i64,
                      semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                    } {}
                  }
                  obelisk.sv.expression.null_literal attributes {
                    node_id = 146 : i64, semantic_type = !obelisk.null
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 114 : i64
            } {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = true,
                node_id = 107 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 108 : i64,
                  referenced_path = "cast_fixture.class_result",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s106.class_result,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$cast",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  dynamic_cast_kind = 1 : i32,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = true, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = true, node_id = 109 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 1 : i32,
                  system_library_cell = "work.cast_fixture",
                  system_scope_path = "cast_fixture",
                  system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 110 : i64,
                    semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 111 : i64,
                      referenced_path = "cast_fixture.class_destination",
                      referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s104.class_destination,
                      semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                    } {}
                    obelisk.sv.expression.empty_argument attributes {
                      is_signed = false, node_id = 112 : i64,
                      semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s100.A>
                    } {}
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 113 : i64,
                    referenced_path = "cast_fixture.class_source",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s105.class_source,
                    semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s102.B>
                  } {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 56 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64, callee_name = "$cast",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0>,
                dynamic_cast_kind = 2 : i32,
                dynamic_cast_enum_values = ["1", "4", "9"],
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_signed = true, is_super_class = false,
                is_system_call = true, node_id = 115 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 1 : i32,
                system_library_cell = "work.cast_fixture",
                system_scope_path = "cast_fixture",
                system_scope_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture
              } {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = true,
                  node_id = 116 : i64,
                  semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 117 : i64,
                    referenced_path = "cast_fixture.enum_destination",
                    referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s5.enum_destination,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {}
                  obelisk.sv.expression.empty_argument attributes {
                    is_signed = true, node_id = 118 : i64,
                    semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {}
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 119 : i64,
                  referenced_path = "cast_fixture.integer_source",
                  referenced_symbol = @s1.$root::@s3.cast_fixture::@s4.cast_fixture::@s6.integer_source,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[INTEGER:.*]] = simulation.ref.load {{.*}} : {{.*}} -> i32
// CHECK-DAG: %[[ENUM1:.*]] = arith.cmpi eq, %[[INTEGER]], {{.*}} : i32
// CHECK-DAG: %[[ENUM4:.*]] = arith.cmpi eq, %[[INTEGER]], {{.*}} : i32
// CHECK-DAG: %[[ENUM9:.*]] = arith.cmpi eq, %[[INTEGER]], {{.*}} : i32
// CHECK: %[[ENUM_OK:.*]] = arith.ori
// CHECK: cf.cond_br %[[ENUM_OK]], ^[[ENUM_STORE:bb[0-9]+]], ^[[ENUM_DONE:bb[0-9]+]]
// CHECK: ^[[ENUM_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[INTEGER]] to %[[ENUM_DEST:[^ ]+]] :
// CHECK-NEXT: cf.br ^[[ENUM_DONE]]
// CHECK: ^[[ENUM_DONE]]:
// CHECK: %[[WIDE_SOURCE:.*]] = simulation.ref.load {{.*}} : {{.*}} -> i32
// CHECK-NEXT: %[[TRUNCATED:.*]] = arith.trunci %[[WIDE_SOURCE]] : i32 to i4
// CHECK: %[[NARROW_OK:.*]] = arith.cmpi eq, %[[WIDE_SOURCE]], {{.*}} : i32
// CHECK: cf.cond_br %[[NARROW_OK]], ^[[NARROW_STORE:bb[0-9]+]], ^[[NARROW_DONE:bb[0-9]+]]
// CHECK: ^[[NARROW_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[TRUNCATED]]
// CHECK-NEXT: cf.br ^[[NARROW_DONE]]
// CHECK: ^[[NARROW_DONE]]:
// CHECK: %[[REAL:.*]] = simulation.ref.load {{.*}} : {{.*}} -> f64
// CHECK-NEXT: %[[INTEGER_VALUE:.*]] = simulation.real.to_integer %[[REAL]] signed = true : i32
// CHECK-NEXT: simulation.ref.store %[[INTEGER_VALUE]] to %[[ENUM_DEST]]
// CHECK: %[[LOGIC:.*]] = simulation.ref.load {{.*}} : {{.*}} -> !simulation.logic<4>
// CHECK: %[[LOGIC1:.*]] = simulation.logic.compare case_eq %[[LOGIC]], {{.*}}
// CHECK: %[[LOGICX:.*]] = simulation.logic.compare case_eq %[[LOGIC]], {{.*}}
// CHECK: %[[LOGIC_OK:.*]] = arith.ori %[[LOGIC1]], %[[LOGICX]] : i1
// CHECK: cf.cond_br %[[LOGIC_OK]], ^[[LOGIC_STORE:bb[0-9]+]], ^[[LOGIC_DONE:bb[0-9]+]]
// CHECK: ^[[LOGIC_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[LOGIC]]
// CHECK-NEXT: cf.br ^[[LOGIC_DONE]]
// CHECK: ^[[LOGIC_DONE]]:
// CHECK: %[[SAME_ENUM:.*]] = simulation.ref.load %[[ENUM_DEST]]
// CHECK: %[[SAME_PARTIAL:.*]] = arith.ori
// CHECK: arith.cmpi eq, %[[SAME_ENUM]],
// CHECK-NEXT: %[[SAME_OK:.*]] = arith.ori %[[SAME_PARTIAL]], {{.*}} : i1
// CHECK: cf.cond_br %[[SAME_OK]], ^[[SAME_STORE:bb[0-9]+]], ^[[SAME_DONE:bb[0-9]+]]
// CHECK: ^[[SAME_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[SAME_ENUM]] to %[[ENUM_DEST]]
// CHECK-NEXT: cf.br ^[[SAME_DONE]]
// CHECK: ^[[SAME_DONE]]:
// CHECK: %[[OTHER_ENUM:.*]] = simulation.ref.load {{.*}} : {{.*}} -> !simulation.logic<4>
// CHECK-NEXT: %[[OTHER_WIDE:.*]] = simulation.logic.resize %[[OTHER_ENUM]] signed = false : !simulation.logic<4> -> !simulation.logic<32>
// CHECK: simulation.logic.compare case_eq %[[OTHER_WIDE]], {{.*}} : (!simulation.logic<32>, !simulation.logic<32>) -> i1
// CHECK: cf.cond_br {{.*}}, ^[[OTHER_STORE:bb[0-9]+]], ^[[OTHER_DONE:bb[0-9]+]]
// CHECK: ^[[OTHER_STORE]]:
// CHECK: simulation.ref.store {{.*}} to %[[ENUM_DEST]]
// CHECK: cf.br ^[[OTHER_DONE]]
// CHECK: ^[[OTHER_DONE]]:
// CHECK: %[[BASE:.*]] = simulation.ref.load {{.*}} : {{.*}} -> !simulation.class_handle<@[[BASE_CLASS:[^>]+]]>
// CHECK-NEXT: %[[DOWNCAST:.*]] = simulation.class.cast %[[BASE]] : !simulation.class_handle<@[[BASE_CLASS]]> to !simulation.class_handle<@[[DERIVED_CLASS:[^>]+]]>
// CHECK-NEXT: %[[INSTANCE:.*]] = simulation.class.is_instance %[[BASE]] is @[[DERIVED_CLASS]]
// CHECK-NEXT: %[[CLASS_ID:.*]] = simulation.class.id %[[BASE]]
// CHECK: %[[IS_NULL:.*]] = arith.cmpi eq, %[[CLASS_ID]], {{.*}} : i64
// CHECK-NEXT: %[[CLASS_OK:.*]] = arith.ori %[[INSTANCE]], %[[IS_NULL]] : i1
// CHECK-NEXT: cf.cond_br %[[CLASS_OK]], ^[[CLASS_STORE:bb[0-9]+]], ^[[CLASS_DONE:bb[0-9]+]]
// CHECK: ^[[CLASS_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[DOWNCAST]] to %[[DERIVED_DEST:[^ ]+]]
// CHECK-NEXT: cf.br ^[[CLASS_DONE]]
// CHECK: ^[[CLASS_DONE]]:
// CHECK: simulation.ref.store {{.*}} to %[[CLASS_RESULT:[^ ]+]] : i32
// CHECK: %[[NULL:.*]] = simulation.class.null {{.*}} : !simulation.class_handle<@[[BASE_CLASS]]>
// CHECK-NEXT: simulation.ref.store %[[NULL]] to %[[BASE_DEST:[^ ]+]]
// CHECK: %[[TRUE:.*]] = arith.constant {{.*}}1 : i32
// CHECK-NEXT: simulation.ref.store %[[TRUE]] to %[[CLASS_RESULT]]
// CHECK-NOT: simulation.class.cast
// CHECK: %[[FALSE:.*]] = arith.constant {{.*}}0 : i32
// CHECK-NEXT: simulation.ref.store %[[FALSE]] to %[[CLASS_RESULT]]
// CHECK: %[[TASK_PARTIAL:.*]] = arith.ori
// CHECK: arith.cmpi eq
// CHECK-NEXT: %[[TASK_OK:.*]] = arith.ori %[[TASK_PARTIAL]], {{.*}} : i1
// CHECK: cf.cond_br %[[TASK_OK]], ^[[TASK_STORE:bb[0-9]+]], ^[[TASK_FAIL:bb[0-9]+]]
// CHECK: ^[[TASK_STORE]]:
// CHECK-NEXT: simulation.ref.store {{.*}} to %[[ENUM_DEST]]
// CHECK-NEXT: simulation.return
// CHECK: ^[[TASK_FAIL]]:
// CHECK: simulation.bytes.constant {{.*}}$cast failed when used as a task
// CHECK: simulation.display
// CHECK: simulation.fatal
// CHECK-NEXT: simulation.return
