// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=0' -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceIntegralOperators
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceIntegralOperators
!u8 = !obelisk.integral<8, false, false, 7 : 0, bit>
!s8 = !obelisk.integral<8, true, false, 7 : 0, bit>
!f8 = !obelisk.integral<8, false, true, 7 : 0, logic>
!u16 = !obelisk.integral<16, false, false, 15 : 0, bit>
!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
obelisk.sv.symbol.definition @source attributes {node_id = 81 : i64, name = "source", hierarchical_name = "source", definition_kind = 0 : i32} {}
obelisk.sv.symbol.root @root attributes {node_id = 80 : i64, name = "$root", hierarchical_name = "$root"} {
  obelisk.sv.symbol.instance @top attributes {node_id = 79 : i64, name = "top", hierarchical_name = "top", referenced_symbol = @source, referenced_path = "source", is_uninstantiated = false} {
    obelisk.sv.symbol.instance_body @body attributes {node_id = 78 : i64, name = "source", hierarchical_name = "top"} {
      obelisk.sv.symbol.variable @a attributes {node_id = 34 : i64, name = "a", hierarchical_name = "top.a", lifetime = 1 : i32, semantic_type = !u8} {}
      obelisk.sv.symbol.variable @b attributes {node_id = 35 : i64, name = "b", hierarchical_name = "top.b", lifetime = 1 : i32, semantic_type = !u8} {}
      obelisk.sv.symbol.variable @negative attributes {node_id = 36 : i64, name = "negative", hierarchical_name = "top.negative", lifetime = 1 : i32, semantic_type = !s8} {}
      obelisk.sv.symbol.variable @condition attributes {node_id = 37 : i64, name = "condition", hierarchical_name = "top.condition", lifetime = 1 : i32, semantic_type = !f8} {}
      obelisk.sv.symbol.variable @sum attributes {node_id = 38 : i64, name = "sum", hierarchical_name = "top.sum", lifetime = 1 : i32, semantic_type = !u8} {}
      obelisk.sv.symbol.variable @widened attributes {node_id = 39 : i64, name = "widened", hierarchical_name = "top.widened", lifetime = 1 : i32, semantic_type = !u16} {}
      obelisk.sv.symbol.variable @unsigned_context attributes {node_id = 40 : i64, name = "unsigned_context", hierarchical_name = "top.unsigned_context", lifetime = 1 : i32, semantic_type = !u16} {}
      obelisk.sv.symbol.variable @less attributes {node_id = 41 : i64, name = "less", hierarchical_name = "top.less", lifetime = 1 : i32, semantic_type = !bit} {}
      obelisk.sv.symbol.variable @choice attributes {node_id = 42 : i64, name = "choice", hierarchical_name = "top.choice", lifetime = 1 : i32, semantic_type = !f8} {}
      obelisk.sv.symbol.variable @inverted attributes {node_id = 43 : i64, name = "inverted", hierarchical_name = "top.inverted", lifetime = 1 : i32, semantic_type = !u8} {}
      obelisk.sv.symbol.variable @quotient attributes {node_id = 44 : i64, name = "quotient", hierarchical_name = "top.quotient", lifetime = 1 : i32, semantic_type = !f8} {}
      obelisk.sv.symbol.variable @sized_cast attributes {node_id = 45 : i64, name = "sized_cast", hierarchical_name = "top.sized_cast", lifetime = 1 : i32, semantic_type = !u8} {}
      obelisk.sv.symbol.procedural_block @init_sum attributes {node_id = 49 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 48 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 47 : i64, assignment_kind = 0 : i32, semantic_type = !u8} {
            obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_symbol = @root::@top::@body::@sum, referenced_path = "top.sum", semantic_type = !u8} {}
            obelisk.sv.expression.binary_op attributes {node_id = 3 : i64, operator_kind = 0 : i32, semantic_type = !u8} {
              obelisk.sv.expression.named_value attributes {node_id = 1 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
              obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_widened attributes {node_id = 53 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 52 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 51 : i64, assignment_kind = 0 : i32, semantic_type = !u16} {
            obelisk.sv.expression.named_value attributes {node_id = 50 : i64, referenced_symbol = @root::@top::@body::@widened, referenced_path = "top.widened", semantic_type = !u16} {}
            obelisk.sv.expression.binary_op attributes {node_id = 10 : i64, operator_kind = 23 : i32, semantic_type = !u16} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 0 : i32, semantic_type = !u16} {
                obelisk.sv.expression.conversion attributes {node_id = 5 : i64, is_implicit = true, semantic_type = !u16} {
                  obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
                }
                obelisk.sv.expression.conversion attributes {node_id = 7 : i64, is_implicit = true, semantic_type = !u16} {
                  obelisk.sv.expression.named_value attributes {node_id = 6 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
                }
              }
              obelisk.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", semantic_type = !u8, is_signed = false} {}
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_unsigned_context attributes {node_id = 57 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 56 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 55 : i64, assignment_kind = 0 : i32, semantic_type = !u16} {
            obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_symbol = @root::@top::@body::@unsigned_context, referenced_path = "top.unsigned_context", semantic_type = !u16} {}
            obelisk.sv.expression.binary_op attributes {node_id = 15 : i64, operator_kind = 0 : i32, semantic_type = !u16} {
              obelisk.sv.expression.unary_op attributes {node_id = 13 : i64, operator_kind = 1 : i32, semantic_type = !u16} {
                obelisk.sv.expression.conversion attributes {node_id = 12 : i64, is_implicit = true, semantic_type = !u16} {
                  obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_symbol = @root::@top::@body::@negative, referenced_path = "top.negative", semantic_type = !s8} {}
                }
              }
              obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "1", semantic_type = !u16, is_signed = false} {}
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_less attributes {node_id = 61 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 59 : i64, assignment_kind = 0 : i32, semantic_type = !bit} {
            obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_symbol = @root::@top::@body::@less, referenced_path = "top.less", semantic_type = !bit} {}
            obelisk.sv.expression.binary_op attributes {node_id = 18 : i64, operator_kind = 16 : i32, semantic_type = !bit} {
              obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_choice attributes {node_id = 65 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 64 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 63 : i64, assignment_kind = 0 : i32, semantic_type = !f8} {
            obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_symbol = @root::@top::@body::@choice, referenced_path = "top.choice", semantic_type = !f8} {}
            obelisk.sv.expression.conditional_op attributes {node_id = 24 : i64, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, semantic_type = !f8} {
              obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_symbol = @root::@top::@body::@condition, referenced_path = "top.condition", semantic_type = !f8} {}
              obelisk.sv.expression.conversion attributes {node_id = 21 : i64, is_implicit = true, semantic_type = !f8} {
                obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
              }
              obelisk.sv.expression.conversion attributes {node_id = 23 : i64, is_implicit = true, semantic_type = !f8} {
                obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
              }
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_inverted attributes {node_id = 69 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 68 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 67 : i64, assignment_kind = 0 : i32, semantic_type = !u8} {
            obelisk.sv.expression.named_value attributes {node_id = 66 : i64, referenced_symbol = @root::@top::@body::@inverted, referenced_path = "top.inverted", semantic_type = !u8} {}
            obelisk.sv.expression.unary_op attributes {node_id = 26 : i64, operator_kind = 2 : i32, semantic_type = !u8} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_quotient attributes {node_id = 73 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 72 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 71 : i64, assignment_kind = 0 : i32, semantic_type = !f8} {
            obelisk.sv.expression.named_value attributes {node_id = 70 : i64, referenced_symbol = @root::@top::@body::@quotient, referenced_path = "top.quotient", semantic_type = !f8} {}
            obelisk.sv.expression.binary_op attributes {node_id = 31 : i64, operator_kind = 3 : i32, semantic_type = !f8} {
              obelisk.sv.expression.conversion attributes {node_id = 28 : i64, is_implicit = true, semantic_type = !f8} {
                obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
              }
              obelisk.sv.expression.conversion attributes {node_id = 30 : i64, is_implicit = true, semantic_type = !f8} {
                obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_symbol = @root::@top::@body::@b, referenced_path = "top.b", semantic_type = !u8} {}
              }
            }
          }
        }
      }
      obelisk.sv.symbol.procedural_block @init_sized_cast attributes {node_id = 77 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        obelisk.sv.statement.expression_statement attributes {node_id = 76 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 75 : i64, assignment_kind = 0 : i32, semantic_type = !u8} {
            obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_symbol = @root::@top::@body::@sized_cast, referenced_path = "top.sized_cast", semantic_type = !u8} {}
            obelisk.sv.expression.conversion attributes {node_id = 33 : i64, is_implicit = false, semantic_type = !u8} {
              obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !u8} {}
            }
          }
        }
      }
    }
  }
}
}
