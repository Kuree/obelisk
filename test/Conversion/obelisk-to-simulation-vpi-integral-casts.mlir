// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.IntegralCasts:VPIConstantEvaluation.IntegralCasts
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.IntegralCasts
// RUN: obelisk-opt %t.opt --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// Cast operands retain assignment context; declarations retain their own types.
!u8 = !obelisk.integral<8, false, false, 7 : 0, bit>
!s8 = !obelisk.integral<8, true, false, 7 : 0, bit>
!f8 = !obelisk.integral<8, false, true, 7 : 0, logic>
!u32 = !obelisk.integral<32, false, false, 31 : 0, int>
!i32 = !obelisk.integral<32, true, false, 31 : 0, int>
!f32 = !obelisk.integral<32, false, true, 31 : 0, logic>
!byte = !obelisk.integral<8, true, false, 7 : 0, byte>
!wide = !obelisk.integral<130, false, false, 129 : 0, bit>
!alias = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @source attributes {node_id = 1 : i64, name = "source", hierarchical_name = "source", definition_kind = 0 : i32} {}
  obelisk.sv.symbol.root @root attributes {node_id = 2 : i64, name = "$root", hierarchical_name = "$root"} {
    obelisk.sv.symbol.instance @top attributes {node_id = 3 : i64, name = "top", hierarchical_name = "top", referenced_symbol = @source, referenced_path = "source", is_uninstantiated = false} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "source", hierarchical_name = "top"} {
        obelisk.sv.type.type_alias @T attributes {node_id = 5 : i64, name = "T", hierarchical_name = "top.T", semantic_type = !alias, vpi_typedef_layers = [{aliases = [@root::@top::@body::@T], path = array<i64>}]} {}
        obelisk.sv.symbol.variable @a attributes {node_id = 6 : i64, name = "a", hierarchical_name = "top.a", lifetime = 1 : i32, semantic_type = !u8} {}
        obelisk.sv.symbol.variable @b attributes {node_id = 7 : i64, name = "b", hierarchical_name = "top.b", lifetime = 1 : i32, semantic_type = !u8} {}
        obelisk.sv.symbol.variable @s attributes {node_id = 8 : i64, name = "s", hierarchical_name = "top.s", lifetime = 1 : i32, semantic_type = !s8} {}
        obelisk.sv.symbol.variable @f attributes {node_id = 9 : i64, name = "f", hierarchical_name = "top.f", lifetime = 1 : i32, semantic_type = !f8} {}
        obelisk.sv.symbol.variable @x attributes {node_id = 10 : i64, name = "x", hierarchical_name = "top.x", lifetime = 1 : i32, semantic_type = !u32} {}
        obelisk.sv.symbol.variable @widen attributes {node_id = 11 : i64, name = "widen", hierarchical_name = "top.widen", lifetime = 1 : i32, semantic_type = !i32} {}
        obelisk.sv.symbol.variable @sign_extend attributes {node_id = 12 : i64, name = "sign_extend", hierarchical_name = "top.sign_extend", lifetime = 1 : i32, semantic_type = !u32} {}
        obelisk.sv.symbol.variable @nested_widen attributes {node_id = 13 : i64, name = "nested_widen", hierarchical_name = "top.nested_widen", lifetime = 1 : i32, semantic_type = !i32} {}
        obelisk.sv.symbol.variable @nested_narrow attributes {node_id = 14 : i64, name = "nested_narrow", hierarchical_name = "top.nested_narrow", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @narrow attributes {node_id = 15 : i64, name = "narrow", hierarchical_name = "top.narrow", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @to_two attributes {node_id = 16 : i64, name = "to_two", hierarchical_name = "top.to_two", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @to_four attributes {node_id = 17 : i64, name = "to_four", hierarchical_name = "top.to_four", lifetime = 1 : i32, semantic_type = !f32} {}
        obelisk.sv.symbol.variable @wide attributes {node_id = 18 : i64, name = "wide", hierarchical_name = "top.wide", lifetime = 1 : i32, semantic_type = !wide} {}
        obelisk.sv.symbol.variable @aliased attributes {node_id = 19 : i64, name = "aliased", hierarchical_name = "top.aliased", lifetime = 1 : i32, semantic_type = !alias, vpi_typedef_layers = [{aliases = [@root::@top::@body::@T], path = array<i64>}]} {}
        obelisk.sv.symbol.variable @nested_boundary attributes {node_id = 20 : i64, name = "nested_boundary", hierarchical_name = "top.nested_boundary", lifetime = 1 : i32, semantic_type = !u32} {}
        obelisk.sv.symbol.variable @const_cast attributes {node_id = 21 : i64, name = "const_cast", hierarchical_name = "top.const_cast", lifetime = 1 : i32, semantic_type = !u8} {}
        obelisk.sv.symbol.procedural_block @init_widen attributes {node_id = 58 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 56 : i64, assignment_kind = 0 : i32, semantic_type = !i32} {
              obelisk.sv.expression.named_value attributes {node_id = 55 : i64, referenced_symbol = @root::@top::@body::@widen, referenced_path = "top.widen", semantic_type = !i32} {}
              obelisk.sv.expression.conversion attributes {node_id = 23 : i64, is_implicit = false, semantic_type = !i32} {
                obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_sign_extend attributes {node_id = 62 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 61 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 60 : i64, assignment_kind = 0 : i32, semantic_type = !u32} {
              obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_symbol = @root::@top::@body::@sign_extend, referenced_path = "top.sign_extend", semantic_type = !u32} {}
              obelisk.sv.expression.conversion attributes {node_id = 25 : i64, is_implicit = false, semantic_type = !u32} {
                obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_symbol = @root::@top::@body::@s, referenced_path = "top.s", semantic_type = !s8} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_nested_widen attributes {node_id = 66 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 65 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 64 : i64, assignment_kind = 0 : i32, semantic_type = !i32} {
              obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_symbol = @root::@top::@body::@nested_widen, referenced_path = "top.nested_widen", semantic_type = !i32} {}
              obelisk.sv.expression.conversion attributes {node_id = 31 : i64, is_implicit = false, semantic_type = !i32} {
                obelisk.sv.expression.binary_op attributes {node_id = 30 : i64, operator_kind = 0 : i32, semantic_type = !u32} {
                  obelisk.sv.expression.conversion attributes {node_id = 27 : i64, is_implicit = true, semantic_type = !u32} {
                    obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 29 : i64, is_implicit = true, semantic_type = !u32} {
                    obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_nested_narrow attributes {node_id = 70 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 69 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 68 : i64, assignment_kind = 0 : i32, semantic_type = !byte} {
              obelisk.sv.expression.named_value attributes {node_id = 67 : i64, referenced_symbol = @root::@top::@body::@nested_narrow, referenced_path = "top.nested_narrow", semantic_type = !byte} {}
              obelisk.sv.expression.conversion attributes {node_id = 35 : i64, is_implicit = false, semantic_type = !byte} {
                obelisk.sv.expression.binary_op attributes {node_id = 34 : i64, operator_kind = 3 : i32, semantic_type = !f32} {
                  obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_symbol = @root::@top::@body::@x, referenced_path = "top.x", semantic_type = !u32} {}
                  obelisk.sv.expression.integer_literal attributes {node_id = 33 : i64, constant_value = "2", semantic_type = !u32} {}
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_narrow attributes {node_id = 74 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 73 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 72 : i64, assignment_kind = 0 : i32, semantic_type = !byte} {
              obelisk.sv.expression.named_value attributes {node_id = 71 : i64, referenced_symbol = @root::@top::@body::@narrow, referenced_path = "top.narrow", semantic_type = !byte} {}
              obelisk.sv.expression.conversion attributes {node_id = 37 : i64, is_implicit = false, semantic_type = !byte} {
                obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_symbol = @root::@top::@body::@x, referenced_path = "top.x", semantic_type = !u32} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_to_two attributes {node_id = 78 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 77 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 76 : i64, assignment_kind = 0 : i32, semantic_type = !byte} {
              obelisk.sv.expression.named_value attributes {node_id = 75 : i64, referenced_symbol = @root::@top::@body::@to_two, referenced_path = "top.to_two", semantic_type = !byte} {}
              obelisk.sv.expression.conversion attributes {node_id = 39 : i64, is_implicit = false, semantic_type = !byte} {
                obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_symbol = @root::@top::@body::@f, referenced_path = "top.f", semantic_type = !f8} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_to_four attributes {node_id = 82 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 81 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 80 : i64, assignment_kind = 0 : i32, semantic_type = !f32} {
              obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_symbol = @root::@top::@body::@to_four, referenced_path = "top.to_four", semantic_type = !f32} {}
              obelisk.sv.expression.conversion attributes {node_id = 41 : i64, is_implicit = false, semantic_type = !f32} {
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_symbol = @root::@top::@body::@s, referenced_path = "top.s", semantic_type = !s8} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_wide attributes {node_id = 86 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 85 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 84 : i64, assignment_kind = 0 : i32, semantic_type = !wide} {
              obelisk.sv.expression.named_value attributes {node_id = 83 : i64, referenced_symbol = @root::@top::@body::@wide, referenced_path = "top.wide", semantic_type = !wide} {}
              obelisk.sv.expression.conversion attributes {node_id = 43 : i64, is_implicit = false, semantic_type = !wide} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_symbol = @root::@top::@body::@s, referenced_path = "top.s", semantic_type = !s8} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_aliased attributes {node_id = 90 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 89 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 88 : i64, assignment_kind = 0 : i32, semantic_type = !alias} {
              obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_symbol = @root::@top::@body::@aliased, referenced_path = "top.aliased", semantic_type = !alias} {}
              obelisk.sv.expression.conversion attributes {node_id = 45 : i64, is_implicit = false, semantic_type = !alias, vpi_typedef_layers = [{aliases = [@root::@top::@body::@T], path = array<i64>}]} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_symbol = @root::@top::@body::@f, referenced_path = "top.f", semantic_type = !f8} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_nested_boundary attributes {node_id = 94 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 93 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 92 : i64, assignment_kind = 0 : i32, semantic_type = !u32} {
              obelisk.sv.expression.named_value attributes {node_id = 91 : i64, referenced_symbol = @root::@top::@body::@nested_boundary, referenced_path = "top.nested_boundary", semantic_type = !u32} {}
              obelisk.sv.expression.binary_op attributes {node_id = 52 : i64, operator_kind = 0 : i32, semantic_type = !u32} {
                obelisk.sv.expression.conversion attributes {node_id = 50 : i64, is_implicit = true, semantic_type = !u32} {
                  obelisk.sv.expression.conversion attributes {node_id = 49 : i64, is_implicit = false, semantic_type = !u8} {
                    obelisk.sv.expression.binary_op attributes {node_id = 48 : i64, operator_kind = 0 : i32, semantic_type = !u8} {
                      obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
                      obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
                    }
                  }
                }
                obelisk.sv.expression.integer_literal attributes {node_id = 51 : i64, constant_value = "1", semantic_type = !u32} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @init_const_cast attributes {node_id = 98 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 97 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 96 : i64, assignment_kind = 0 : i32, semantic_type = !u8} {
              obelisk.sv.expression.named_value attributes {node_id = 95 : i64, referenced_symbol = @root::@top::@body::@const_cast, referenced_path = "top.const_cast", semantic_type = !u8} {}
              obelisk.sv.expression.conversion attributes {node_id = 54 : i64, is_implicit = false, semantic_type = !u8, is_const_cast = true} {
                obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
              }
            }
          }
        }
      }
    }
  }
}
