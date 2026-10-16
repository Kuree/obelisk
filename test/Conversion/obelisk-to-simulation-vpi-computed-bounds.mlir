// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=0' --mlir-print-debuginfo -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ComputedBounds:VPIConstantEvaluation.*
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' --mlir-print-debuginfo -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ComputedBounds
// RUN: obelisk-opt %t.opt --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// Bounds retain their source arithmetic; the proof adds no stored values.
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!integer = !obelisk.integral<32, true, true, 31 : 0, integer>
!byte = !obelisk.integral<8, false, false, 7 : 0, bit>
!nibble = !obelisk.integral<4, false, false, 3 : 0, bit>
!wide = !obelisk.integral<65, false, false, 64 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @source attributes {node_id = 99 : i64, name = "source", hierarchical_name = "source", definition_kind = 0 : i32} {}
  obelisk.sv.symbol.definition @shared attributes {node_id = 100 : i64, name = "shared", hierarchical_name = "shared", definition_kind = 0 : i32} {}
  obelisk.sv.symbol.root @root attributes {node_id = 103 : i64, name = "$root", hierarchical_name = "$root"} {
    obelisk.sv.symbol.instance @top attributes {node_id = 102 : i64, name = "top", hierarchical_name = "top", referenced_symbol = @source, referenced_path = "source", is_uninstantiated = false} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 101 : i64, name = "source", hierarchical_name = "top"} {
        obelisk.sv.symbol.parameter @N attributes {node_id = 1 : i64, hierarchical_name = "top.N", name = "N", constant_value = "4", semantic_type = !int} {}
        obelisk.sv.symbol.variable @a attributes {node_id = 2 : i64, hierarchical_name = "top.a", name = "a", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @base attributes {node_id = 3 : i64, hierarchical_name = "top.base", name = "base", lifetime = 1 : i32, semantic_type = !int} {}
        obelisk.sv.symbol.variable @part attributes {node_id = 4 : i64, hierarchical_name = "top.part", name = "part", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.variable @indexed attributes {node_id = 5 : i64, hierarchical_name = "top.indexed", name = "indexed", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.variable @repeat attributes {node_id = 6 : i64, hierarchical_name = "top.repeat", name = "repeat", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @wide attributes {node_id = 7 : i64, hierarchical_name = "top.wide", name = "wide", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.variable @unsupported attributes {node_id = 8 : i64, hierarchical_name = "top.unsupported", name = "unsupported", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.procedural_block @init_part attributes {node_id = 20 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 18 : i64, assignment_kind = 0 : i32, semantic_type = !nibble} {
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_symbol = @root::@top::@body::@part, referenced_path = "top.part", semantic_type = !nibble} {}
              obelisk.sv.expression.range_select attributes {node_id = 16 : i64, selection_kind = 0 : i32, semantic_type = !nibble} {
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !byte} {}
                obelisk.sv.expression.binary_op attributes {node_id = 11 : i64, operator_kind = 0 : i32, semantic_type = !int, folded_constant = "7"} {
                  obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_symbol = @root::@top::@body::@N, referenced_path = "top.N", semantic_type = !int} {}
                  obelisk.sv.expression.integer_literal attributes {node_id = 10 : i64, constant_value = "3", semantic_type = !int} {}

                }
                obelisk.sv.expression.binary_op attributes {node_id = 14 : i64, operator_kind = 1 : i32, semantic_type = !int, folded_constant = "4"} {
                  obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_symbol = @root::@top::@body::@N, referenced_path = "top.N", semantic_type = !int} {}
                  obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "0", semantic_type = !int} {}

                }

              }

            }

          } loc("computed_bounds.sv":10:1)

        }
        obelisk.sv.symbol.procedural_block @init_indexed attributes {node_id = 32 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 30 : i64, assignment_kind = 0 : i32, semantic_type = !nibble} {
              obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_symbol = @root::@top::@body::@indexed, referenced_path = "top.indexed", semantic_type = !nibble} {}
              obelisk.sv.expression.range_select attributes {node_id = 28 : i64, selection_kind = 1 : i32, semantic_type = !nibble} {
                obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !byte} {}
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_symbol = @root::@top::@body::@base, referenced_path = "top.base", semantic_type = !int} {}
                obelisk.sv.expression.binary_op attributes {node_id = 26 : i64, operator_kind = 2 : i32, semantic_type = !int, folded_constant = "4"} {
                  obelisk.sv.expression.binary_op attributes {node_id = 24 : i64, operator_kind = 0 : i32, semantic_type = !int, folded_constant = "4"} {
                    obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_symbol = @root::@top::@body::@N, referenced_path = "top.N", semantic_type = !int} {}
                    obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64, constant_value = "0", semantic_type = !int} {}

                  }
                  obelisk.sv.expression.integer_literal attributes {node_id = 25 : i64, constant_value = "1", semantic_type = !int} {}

                }

              }

            }

          } loc("computed_bounds.sv":20:1)

        }
        obelisk.sv.symbol.procedural_block @init_repeat attributes {node_id = 45 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 43 : i64, assignment_kind = 0 : i32, semantic_type = !byte} {
              obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_symbol = @root::@top::@body::@repeat, referenced_path = "top.repeat", semantic_type = !byte} {}
              obelisk.sv.expression.replication attributes {node_id = 41 : i64, semantic_type = !byte} {
                obelisk.sv.expression.binary_op attributes {node_id = 40 : i64, operator_kind = 1 : i32, semantic_type = !integer, folded_constant = "2"} {
                  obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_symbol = @root::@top::@body::@N, referenced_path = "top.N", semantic_type = !int} {}
                  obelisk.sv.expression.integer_literal attributes {node_id = 39 : i64, constant_value = "2", semantic_type = !integer} {}

                }
                obelisk.sv.expression.concatenation attributes {node_id = 37 : i64, semantic_type = !nibble} {
                  obelisk.sv.expression.range_select attributes {node_id = 36 : i64, selection_kind = 0 : i32, semantic_type = !nibble} {
                    obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !byte} {}
                    obelisk.sv.expression.integer_literal attributes {node_id = 33 : i64, constant_value = "3", semantic_type = !int} {}
                    obelisk.sv.expression.integer_literal attributes {node_id = 34 : i64, constant_value = "0", semantic_type = !int} {}

                  }

                }

              }

            }

          } loc("computed_bounds.sv":30:1)

        }
        obelisk.sv.symbol.procedural_block @init_wide attributes {node_id = 57 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 56 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 55 : i64, assignment_kind = 0 : i32, semantic_type = !nibble} {
              obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_symbol = @root::@top::@body::@wide, referenced_path = "top.wide", semantic_type = !nibble} {}
              obelisk.sv.expression.range_select attributes {node_id = 53 : i64, selection_kind = 0 : i32, semantic_type = !nibble} {
                obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !byte} {}
                obelisk.sv.expression.binary_op attributes {node_id = 48 : i64, operator_kind = 0 : i32, semantic_type = !wide, folded_constant = "7"} {
                  obelisk.sv.expression.integer_literal attributes {node_id = 46 : i64, constant_value = "3", semantic_type = !wide} {}
                  obelisk.sv.expression.integer_literal attributes {node_id = 47 : i64, constant_value = "4", semantic_type = !wide} {}

                }
                obelisk.sv.expression.binary_op attributes {node_id = 51 : i64, operator_kind = 1 : i32, semantic_type = !wide, folded_constant = "4"} {
                  obelisk.sv.expression.integer_literal attributes {node_id = 49 : i64, constant_value = "5", semantic_type = !wide} {}
                  obelisk.sv.expression.integer_literal attributes {node_id = 50 : i64, constant_value = "1", semantic_type = !wide} {}

                }

              }

            }

          } loc("computed_bounds.sv":40:1)

        }
        obelisk.sv.symbol.procedural_block @init_unsupported attributes {node_id = 68 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 67 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 66 : i64, assignment_kind = 0 : i32, semantic_type = !nibble} {
              obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_symbol = @root::@top::@body::@unsupported, referenced_path = "top.unsupported", semantic_type = !nibble} {}
              obelisk.sv.expression.range_select attributes {node_id = 64 : i64, selection_kind = 0 : i32, semantic_type = !nibble} {
                obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !byte} {}
                obelisk.sv.expression.conversion attributes {node_id = 61 : i64, is_implicit = false, semantic_type = !int, folded_constant = "7"} {
                  obelisk.sv.expression.binary_op attributes {node_id = 60 : i64, operator_kind = 0 : i32, semantic_type = !int, folded_constant = "7"} {
                    obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_symbol = @root::@top::@body::@N, referenced_path = "top.N", semantic_type = !int} {}
                    obelisk.sv.expression.integer_literal attributes {node_id = 59 : i64, constant_value = "3", semantic_type = !int} {}

                  }

                }
                obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_symbol = @root::@top::@body::@N, referenced_path = "top.N", semantic_type = !int} {}

              }

            }

          } loc("computed_bounds.sv":60:1)

        }

      }

    }
    obelisk.sv.symbol.instance @left attributes {node_id = 83 : i64, name = "left", hierarchical_name = "left", referenced_symbol = @shared, referenced_path = "shared", is_uninstantiated = false} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 82 : i64, name = "shared", hierarchical_name = "left"} {
        obelisk.sv.symbol.parameter @N attributes {node_id = 69 : i64, hierarchical_name = "left.N", name = "N", constant_value = "1", semantic_type = !int} {}
        obelisk.sv.symbol.variable @a attributes {node_id = 70 : i64, hierarchical_name = "left.a", name = "a", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @part attributes {node_id = 71 : i64, hierarchical_name = "left.part", name = "part", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.procedural_block @init_part attributes {node_id = 81 : i64, hierarchical_name = "left", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 79 : i64, assignment_kind = 0 : i32, semantic_type = !nibble} {
              obelisk.sv.expression.named_value attributes {node_id = 78 : i64, referenced_symbol = @root::@left::@body::@part, referenced_path = "left.part", semantic_type = !nibble} {}
              obelisk.sv.expression.range_select attributes {node_id = 77 : i64, selection_kind = 0 : i32, semantic_type = !nibble} {
                obelisk.sv.expression.named_value attributes {node_id = 76 : i64, referenced_symbol = @root::@left::@body::@a, referenced_path = "left.a", semantic_type = !byte} {} loc("computed_bounds.sv":50:1)
                obelisk.sv.expression.binary_op attributes {node_id = 74 : i64, operator_kind = 0 : i32, semantic_type = !int, folded_constant = "4"} {
                  obelisk.sv.expression.named_value attributes {node_id = 72 : i64, referenced_symbol = @root::@left::@body::@N, referenced_path = "left.N", semantic_type = !int} {} loc("computed_bounds.sv":50:1)
                  obelisk.sv.expression.integer_literal attributes {node_id = 73 : i64, constant_value = "3", semantic_type = !int} {} loc("computed_bounds.sv":50:1)

                } loc("computed_bounds.sv":50:1)
                obelisk.sv.expression.named_value attributes {node_id = 75 : i64, referenced_symbol = @root::@left::@body::@N, referenced_path = "left.N", semantic_type = !int} {} loc("computed_bounds.sv":50:1)

              } loc("computed_bounds.sv":50:1)

            }

          } loc("computed_bounds.sv":50:1)

        }

      }

    }
    obelisk.sv.symbol.instance @right attributes {node_id = 98 : i64, name = "right", hierarchical_name = "right", referenced_symbol = @shared, referenced_path = "shared", is_uninstantiated = false} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 97 : i64, name = "shared", hierarchical_name = "right"} {
        obelisk.sv.symbol.parameter @N attributes {node_id = 84 : i64, hierarchical_name = "right.N", name = "N", constant_value = "2", semantic_type = !int} {}
        obelisk.sv.symbol.variable @a attributes {node_id = 85 : i64, hierarchical_name = "right.a", name = "a", lifetime = 1 : i32, semantic_type = !byte} {}
        obelisk.sv.symbol.variable @part attributes {node_id = 86 : i64, hierarchical_name = "right.part", name = "part", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.procedural_block @init_part attributes {node_id = 96 : i64, hierarchical_name = "right", procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 95 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 94 : i64, assignment_kind = 0 : i32, semantic_type = !nibble} {
              obelisk.sv.expression.named_value attributes {node_id = 93 : i64, referenced_symbol = @root::@right::@body::@part, referenced_path = "right.part", semantic_type = !nibble} {}
              obelisk.sv.expression.range_select attributes {node_id = 92 : i64, selection_kind = 0 : i32, semantic_type = !nibble} {
                obelisk.sv.expression.named_value attributes {node_id = 91 : i64, referenced_symbol = @root::@right::@body::@a, referenced_path = "right.a", semantic_type = !byte} {} loc("computed_bounds.sv":50:1)
                obelisk.sv.expression.binary_op attributes {node_id = 89 : i64, operator_kind = 0 : i32, semantic_type = !int, folded_constant = "5"} {
                  obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_symbol = @root::@right::@body::@N, referenced_path = "right.N", semantic_type = !int} {} loc("computed_bounds.sv":50:1)
                  obelisk.sv.expression.integer_literal attributes {node_id = 88 : i64, constant_value = "3", semantic_type = !int} {} loc("computed_bounds.sv":50:1)

                } loc("computed_bounds.sv":50:1)
                obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_symbol = @root::@right::@body::@N, referenced_path = "right.N", semantic_type = !int} {} loc("computed_bounds.sv":50:1)

              } loc("computed_bounds.sv":50:1)

            }

          } loc("computed_bounds.sv":50:1)

        }

      }

    }

  }
}
