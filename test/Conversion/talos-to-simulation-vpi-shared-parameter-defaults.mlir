// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedParameterDefaults
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedParameterDefaults
!int = !talos.integral<32, true, false, 31 : 0, int>
!byte = !talos.integral<8, false, false, 7 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @cell attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "cell", name = "cell"} {}
  talos.sv.symbol.root @root attributes {node_id = 2 : i64, hierarchical_name = "$root", name = "$root"} {
    talos.sv.symbol.instance @left attributes {node_id = 3 : i64, hierarchical_name = "left", is_uninstantiated = false, name = "left", referenced_path = "cell", referenced_symbol = @cell} {
      talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, hierarchical_name = "left", name = "cell"} {
        talos.sv.symbol.parameter @P attributes {node_id = 5 : i64, hierarchical_name = "left.P", name = "P", constant_value = "3", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 6 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 7 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
          }
        }
        talos.sv.symbol.parameter @Q attributes {node_id = 8 : i64, hierarchical_name = "left.Q", name = "Q", constant_value = "99", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 9 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 10 : i64, operator_kind = 0 : i32} {
              talos.sv.source.parameter_reference attributes {node_id = 11 : i64, parameter = @P} {}
              talos.sv.expression.integer_literal attributes {node_id = 12 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
            }
          }
        }
        talos.sv.symbol.parameter @R attributes {node_id = 13 : i64, hierarchical_name = "left.R", name = "R", constant_value = "99", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 14 : i64} {
            talos.sv.source.parameter_reference attributes {node_id = 15 : i64, parameter = @Q} {}
          }
        }
        talos.sv.symbol.parameter @Equal attributes {node_id = 16 : i64, hierarchical_name = "left.Equal", name = "Equal", constant_value = "4", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 17 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 18 : i64, operator_kind = 0 : i32} {
              talos.sv.source.parameter_reference attributes {node_id = 19 : i64, parameter = @P} {}
              talos.sv.expression.integer_literal attributes {node_id = 20 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
            }
          }
        }
        talos.sv.symbol.parameter @Absent attributes {node_id = 21 : i64, hierarchical_name = "left.Absent", name = "Absent", constant_value = "7", semantic_type = !int} {
        }
        talos.sv.symbol.parameter @Unknown attributes {node_id = 22 : i64, hierarchical_name = "left.Unknown", name = "Unknown", constant_value = "8", semantic_type = !int} {
          talos.sv.source.unsupported_default attributes {node_id = 23 : i64, syntax = "unavailable_call()", reason = "unimplemented source form"} {}
        }
        // A supported literal is planned before the unavailable declaration
        // leaf: retaining a partial graph here would leave orphan occurrences.
        talos.sv.symbol.parameter @omitted attributes {node_id = 1000 : i64, hierarchical_name = "left.omitted", name = "omitted", constant_value = "text", semantic_type = !talos.string} {}
        talos.sv.symbol.parameter @Late attributes {node_id = 1001 : i64, hierarchical_name = "left.Late", name = "Late", constant_value = "9", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 1002 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 1003 : i64, operator_kind = 0 : i32} {
              talos.sv.expression.integer_literal attributes {node_id = 1004 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
              talos.sv.source.parameter_reference attributes {node_id = 1005 : i64, parameter = @omitted} {}
            }
          }
        }
      }
    }
    talos.sv.symbol.instance @right attributes {node_id = 24 : i64, hierarchical_name = "right", is_uninstantiated = false, name = "right", referenced_path = "cell", referenced_symbol = @cell} {
      talos.sv.symbol.instance_body @body attributes {node_id = 25 : i64, hierarchical_name = "right", name = "cell"} {
        talos.sv.symbol.parameter @P attributes {node_id = 26 : i64, hierarchical_name = "right.P", name = "P", constant_value = "3", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 27 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 28 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
          }
        }
        talos.sv.symbol.parameter @Q attributes {node_id = 29 : i64, hierarchical_name = "right.Q", name = "Q", constant_value = "99", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 30 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 31 : i64, operator_kind = 0 : i32} {
              talos.sv.source.parameter_reference attributes {node_id = 32 : i64, parameter = @P} {}
              talos.sv.expression.integer_literal attributes {node_id = 33 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
            }
          }
        }
        talos.sv.symbol.parameter @R attributes {node_id = 34 : i64, hierarchical_name = "right.R", name = "R", constant_value = "99", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 35 : i64} {
            talos.sv.source.parameter_reference attributes {node_id = 36 : i64, parameter = @Q} {}
          }
        }
        talos.sv.symbol.parameter @Equal attributes {node_id = 37 : i64, hierarchical_name = "right.Equal", name = "Equal", constant_value = "4", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 38 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 39 : i64, operator_kind = 0 : i32} {
              talos.sv.source.parameter_reference attributes {node_id = 40 : i64, parameter = @P} {}
              talos.sv.expression.integer_literal attributes {node_id = 41 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
            }
          }
        }
        talos.sv.symbol.parameter @Absent attributes {node_id = 42 : i64, hierarchical_name = "right.Absent", name = "Absent", constant_value = "7", semantic_type = !int} {
        }
        talos.sv.symbol.parameter @Unknown attributes {node_id = 43 : i64, hierarchical_name = "right.Unknown", name = "Unknown", constant_value = "8", semantic_type = !int} {
          talos.sv.source.unsupported_default attributes {node_id = 44 : i64, syntax = "unavailable_call()", reason = "unimplemented source form"} {}
        }
        // A supported literal is planned before the unavailable declaration
        // leaf: retaining a partial graph here would leave orphan occurrences.
        talos.sv.symbol.parameter @omitted attributes {node_id = 1010 : i64, hierarchical_name = "right.omitted", name = "omitted", constant_value = "text", semantic_type = !talos.string} {}
        talos.sv.symbol.parameter @Late attributes {node_id = 1011 : i64, hierarchical_name = "right.Late", name = "Late", constant_value = "9", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 1012 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 1013 : i64, operator_kind = 0 : i32} {
              talos.sv.expression.integer_literal attributes {node_id = 1014 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
              talos.sv.source.parameter_reference attributes {node_id = 1015 : i64, parameter = @omitted} {}
            }
          }
        }
      }
    }
    talos.sv.symbol.instance @different attributes {node_id = 45 : i64, hierarchical_name = "different", is_uninstantiated = false, name = "different", referenced_path = "cell", referenced_symbol = @cell} {
      talos.sv.symbol.instance_body @body attributes {node_id = 46 : i64, hierarchical_name = "different", name = "cell"} {
        talos.sv.symbol.parameter @P attributes {node_id = 47 : i64, hierarchical_name = "different.P", name = "P", constant_value = "5", semantic_type = !byte} {
          talos.sv.source.value_default attributes {node_id = 48 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 49 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
          }
        }
        talos.sv.symbol.parameter @Q attributes {node_id = 50 : i64, hierarchical_name = "different.Q", name = "Q", constant_value = "101", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 51 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 52 : i64, operator_kind = 0 : i32} {
              talos.sv.source.parameter_reference attributes {node_id = 53 : i64, parameter = @P} {}
              talos.sv.expression.integer_literal attributes {node_id = 54 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
            }
          }
        }
        talos.sv.symbol.parameter @R attributes {node_id = 55 : i64, hierarchical_name = "different.R", name = "R", constant_value = "101", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 56 : i64} {
            talos.sv.source.parameter_reference attributes {node_id = 57 : i64, parameter = @Q} {}
          }
        }
        talos.sv.symbol.parameter @Equal attributes {node_id = 58 : i64, hierarchical_name = "different.Equal", name = "Equal", constant_value = "6", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 59 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 60 : i64, operator_kind = 0 : i32} {
              talos.sv.source.parameter_reference attributes {node_id = 61 : i64, parameter = @P} {}
              talos.sv.expression.integer_literal attributes {node_id = 62 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
            }
          }
        }
        talos.sv.symbol.parameter @Absent attributes {node_id = 63 : i64, hierarchical_name = "different.Absent", name = "Absent", constant_value = "7", semantic_type = !int} {
        }
        talos.sv.symbol.parameter @Unknown attributes {node_id = 64 : i64, hierarchical_name = "different.Unknown", name = "Unknown", constant_value = "8", semantic_type = !int} {
          talos.sv.source.unsupported_default attributes {node_id = 65 : i64, syntax = "unavailable_call()", reason = "unimplemented source form"} {}
        }
        // A supported literal is planned before the unavailable declaration
        // leaf: retaining a partial graph here would leave orphan occurrences.
        talos.sv.symbol.parameter @omitted attributes {node_id = 1020 : i64, hierarchical_name = "different.omitted", name = "omitted", constant_value = "text", semantic_type = !talos.string} {}
        talos.sv.symbol.parameter @Late attributes {node_id = 1021 : i64, hierarchical_name = "different.Late", name = "Late", constant_value = "9", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 1022 : i64} {
            talos.sv.source.binary_expression attributes {node_id = 1023 : i64, operator_kind = 0 : i32} {
              talos.sv.expression.integer_literal attributes {node_id = 1024 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
              talos.sv.source.parameter_reference attributes {node_id = 1025 : i64, parameter = @omitted} {}
            }
          }
        }
      }
    }
  }
}
