// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' --mlir-print-debuginfo -o %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.RTLDeclarationExpressions
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=3' --mlir-print-debuginfo -o %t.opt.mlir
// RUN: env OBELISK_TEST_INPUT=%t.opt.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.RTLDeclarationExpressions
// Direct whole declaration uses retain exact identity before optimization.
// Selects and conversions remain unavailable until their own expressions exist.
!word = !obelisk.integral<32, true, false, 31 : 0, int>
!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @definition attributes {node_id = 82 : i64, hierarchical_name = "top", name = "top", definition_kind = 0 : i32} {

  }
  obelisk.sv.symbol.root @root attributes {node_id = 81 : i64, hierarchical_name = "$root", name = "$root"} {
    obelisk.sv.symbol.package @pkg attributes {node_id = 6 : i64, hierarchical_name = "pkg", name = "pkg"} {
      obelisk.sv.symbol.parameter @p attributes {node_id = 5 : i64, hierarchical_name = "pkg::p", name = "p", constant_value = "9", semantic_type = !word} {

      }

    }
    obelisk.sv.symbol.instance @top attributes {node_id = 80 : i64, hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 79 : i64, hierarchical_name = "top", name = "top"} {
        obelisk.sv.symbol.variable @q attributes {node_id = 1 : i64, hierarchical_name = "top.q", name = "q", lifetime = 1 : i32, semantic_type = !word} {

        }
        obelisk.sv.symbol.variable @d attributes {node_id = 2 : i64, hierarchical_name = "top.d", name = "d", lifetime = 1 : i32, semantic_type = !word} {

        }
        obelisk.sv.symbol.variable @flag attributes {node_id = 3 : i64, hierarchical_name = "top.flag", name = "flag", lifetime = 1 : i32, semantic_type = !bit} {

        }
        obelisk.sv.symbol.net @n attributes {node_id = 4 : i64, hierarchical_name = "top.n", name = "n", is_implicit = false, net_kind = 1 : i32, semantic_type = !word} {

        }
        obelisk.sv.symbol.procedural_block @initial attributes {node_id = 59 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.list attributes {node_id = 58 : i64} {
            obelisk.sv.statement.conditional attributes {node_id = 16 : i64, check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = true} {
              obelisk.sv.expression.named_value attributes {node_id = 7 : i64, referenced_symbol = @root::@top::@body::@flag, referenced_path = "top.flag", semantic_type = !bit} {

              }
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.assignment attributes {node_id = 10 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                  obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                  }
                  obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_symbol = @root::@top::@body::@d, referenced_path = "top.d", semantic_type = !word} {

                  }

                }

              } loc("rtl_expressions.sv":11:1)
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {node_id = 14 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                  obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                  }
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                  }

                }

              } loc("rtl_expressions.sv":12:1)

            } loc("rtl_expressions.sv":10:1)
            obelisk.sv.statement.while_loop attributes {node_id = 19 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_symbol = @root::@top::@body::@flag, referenced_path = "top.flag", semantic_type = !bit} {

              }
              obelisk.sv.statement.empty attributes {node_id = 18 : i64} {

              }

            } loc("rtl_expressions.sv":20:1)
            obelisk.sv.statement.do_while_loop attributes {node_id = 22 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_symbol = @root::@top::@body::@flag, referenced_path = "top.flag", semantic_type = !bit} {

              }
              obelisk.sv.statement.empty attributes {node_id = 21 : i64} {

              }

            } loc("rtl_expressions.sv":30:1)
            obelisk.sv.statement.repeat_loop attributes {node_id = 25 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_symbol = @root::@top::@body::@d, referenced_path = "top.d", semantic_type = !word} {

              }
              obelisk.sv.statement.empty attributes {node_id = 24 : i64} {

              }

            } loc("rtl_expressions.sv":40:1)
            obelisk.sv.statement.for_loop attributes {node_id = 34 : i64, initializer_count = 1 : i64, has_condition = true, step_count = 1 : i64} {
              obelisk.sv.expression.assignment attributes {node_id = 28 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                }
                obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_symbol = @root::@top::@body::@d, referenced_path = "top.d", semantic_type = !word} {

                }

              } loc("rtl_expressions.sv":51:1)
              obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_symbol = @root::@top::@body::@flag, referenced_path = "top.flag", semantic_type = !bit} {

              }
              obelisk.sv.expression.assignment attributes {node_id = 32 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                }
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                }

              } loc("rtl_expressions.sv":52:1)
              obelisk.sv.statement.empty attributes {node_id = 33 : i64} {

              }

            } loc("rtl_expressions.sv":50:1)
            obelisk.sv.statement.case attributes {node_id = 39 : i64, condition_kind = 0 : i32, check_kind = 0 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = true} {
              obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

              }
              obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_symbol = @root::@top::@body::@d, referenced_path = "top.d", semantic_type = !word} {

              }
              obelisk.sv.statement.empty attributes {node_id = 37 : i64} {

              }
              obelisk.sv.statement.empty attributes {node_id = 38 : i64} {

              }

            } loc("rtl_expressions.sv":60:1)
            obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
              obelisk.sv.expression.assignment attributes {node_id = 42 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                }
                obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_symbol = @root::@top::@body::@n, referenced_path = "top.n", semantic_type = !word} {

                }

              }

            } loc("rtl_expressions.sv":70:1)
            obelisk.sv.statement.conditional attributes {node_id = 46 : i64, check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
              obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_symbol = @root::@pkg::@p, referenced_path = "pkg::p", semantic_type = !word} {

              }
              obelisk.sv.statement.empty attributes {node_id = 45 : i64} {

              }

            } loc("rtl_expressions.sv":80:1)
            obelisk.sv.statement.expression_statement attributes {node_id = 52 : i64} {
              obelisk.sv.expression.assignment attributes {node_id = 51 : i64, assignment_kind = 0 : i32, semantic_type = !bit} {
                obelisk.sv.expression.named_value attributes {node_id = 50 : i64, referenced_symbol = @root::@top::@body::@flag, referenced_path = "top.flag", semantic_type = !bit} {

                }
                obelisk.sv.expression.element_select attributes {node_id = 49 : i64, semantic_type = !bit} {
                  obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                  }
                  obelisk.sv.expression.integer_literal attributes {node_id = 48 : i64, constant_value = "0", semantic_type = !word} {

                  }

                }

              }

            } loc("rtl_expressions.sv":90:1)
            obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
              obelisk.sv.expression.assignment attributes {node_id = 56 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                obelisk.sv.expression.named_value attributes {node_id = 55 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {

                }
                obelisk.sv.expression.conversion attributes {node_id = 54 : i64, conversion_kind = 0 : i32, semantic_type = !word} {
                  obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_symbol = @root::@top::@body::@flag, referenced_path = "top.flag", semantic_type = !bit} {

                  }

                }

              }

            } loc("rtl_expressions.sv":91:1)

          }

        }
        obelisk.sv.symbol.subroutine @value attributes {node_id = 62 : i64, hierarchical_name = "top.value", name = "value", subroutine_kind = 0 : i32, semantic_type = !obelisk.subroutine<() -> !word, false>} {
          obelisk.sv.statement.return attributes {node_id = 61 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_symbol = @root::@top::@body::@d, referenced_path = "top.d", semantic_type = !word} {

            }

          } loc("rtl_expressions.sv":100:1)

        }
        obelisk.sv.symbol.subroutine @work attributes {node_id = 73 : i64, default_lifetime = 0 : i32, hierarchical_name = "top.work", name = "work", subroutine_kind = 1 : i32, semantic_type = !obelisk.subroutine<() -> (), true>} {
          obelisk.sv.statement.list attributes {node_id = 71 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 63 : i64, referenced_symbol = @root::@top::@body::@work::@local, referenced_path = "top.work.local"} {

            }
            obelisk.sv.statement.expression_statement attributes {node_id = 67 : i64} {
              obelisk.sv.expression.assignment attributes {node_id = 66 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                obelisk.sv.expression.named_value attributes {node_id = 64 : i64, referenced_symbol = @root::@top::@body::@work::@local, referenced_path = "top.work.local", semantic_type = !word} {

                }
                obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_symbol = @root::@top::@body::@d, referenced_path = "top.d", semantic_type = !word} {

                }

              }

            } loc("rtl_expressions.sv":111:1)
            obelisk.sv.statement.conditional attributes {node_id = 70 : i64, check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
              obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_symbol = @root::@top::@body::@work::@local, referenced_path = "top.work.local", semantic_type = !word} {

              }
              obelisk.sv.statement.empty attributes {node_id = 69 : i64} {

              }

            } loc("rtl_expressions.sv":112:1)

          }
          obelisk.sv.symbol.variable @local attributes {node_id = 72 : i64, hierarchical_name = "top.work.local", name = "local", lifetime = 0 : i32, semantic_type = !word} {

          }

        }
        obelisk.sv.symbol.subroutine @alias attributes {node_id = 78 : i64, default_lifetime = 0 : i32, hierarchical_name = "top.alias", name = "alias", subroutine_kind = 1 : i32, semantic_type = !obelisk.subroutine<(!word) -> (), true>} {
          obelisk.sv.statement.conditional attributes {node_id = 76 : i64, check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
            obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_symbol = @root::@top::@body::@alias::@shared, referenced_path = "top.alias.shared", semantic_type = !word} {

            }
            obelisk.sv.statement.empty attributes {node_id = 75 : i64} {

            }

          } loc("rtl_expressions.sv":120:1)
          obelisk.sv.symbol.formal_argument @shared attributes {node_id = 77 : i64, direction = 3 : i32, hierarchical_name = "top.alias.shared", name = "shared", semantic_type = !word} {

          }

        }

      }

    }

  }
}
