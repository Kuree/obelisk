// RUN: talos-opt %s "--lower-talos-to-sim=vpi=read opt-level=0" --mlir-print-debuginfo -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedParameterAssignments*
// RUN: talos-opt %s "--lower-talos-to-sim=vpi=full opt-level=3" --mlir-print-debuginfo -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedParameterAssignments*
// RUN: talos-opt %t.opt --convert-talos-sim-processes-to-llvm-coroutines -o %t.llvm
// RUN: talos-opt %s "--lower-talos-to-sim=vpi=off opt-level=3" "--encode-talos-sim-to-bytecode=vpi=off" | FileCheck %s --check-prefix=OFF
// OFF-NOT: runtime.design_database
// OFF: runtime.execution_flags = #runtime.execution_flags<has_bytecode>
// OFF-NOT: runtime.design_database
!int = !talos.integral<32, true, false, 31 : 0, int>
!byte = !talos.integral<8, true, false, 7 : 0, byte>
!bit = !talos.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @parent attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "parent", name = "parent"} {
  }
  talos.sv.symbol.definition @child attributes {node_id = 2 : i64, definition_kind = 0 : i32, hierarchical_name = "child", name = "child"} {
  }
  talos.sv.symbol.root @root attributes {node_id = 3 : i64, hierarchical_name = "$root", name = "$root"} {
    talos.sv.symbol.instance @left attributes {node_id = 4 : i64, hierarchical_name = "left", is_uninstantiated = false, name = "left", referenced_path = "parent", referenced_symbol = @parent} {
      talos.sv.symbol.instance_body @left_body attributes {node_id = 5 : i64, hierarchical_name = "left", name = "parent"} {
        talos.sv.symbol.parameter @P attributes {node_id = 6 : i64, hierarchical_name = "left.P", name = "P", semantic_type = !int, constant_value = "2"} {
        }
        talos.sv.symbol.type_parameter @T attributes {node_id = 7 : i64, hierarchical_name = "left.T", name = "T", semantic_type = !int} {
        }
        talos.sv.symbol.instance @a attributes {node_id = 8 : i64, hierarchical_name = "left.a", is_uninstantiated = false, name = "a", referenced_path = "child", referenced_symbol = @child} {
          talos.sv.symbol.instance_body @body attributes {node_id = 9 : i64, hierarchical_name = "left.a", name = "child"} {
            talos.sv.symbol.parameter @N attributes {node_id = 10 : i64, hierarchical_name = "left.a.N", name = "N", semantic_type = !int, constant_value = "7"} {
              talos.sv.source.value_default attributes {node_id = 11 : i64} {
                talos.sv.expression.integer_literal attributes {node_id = 12 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {
                }
              }
            }
            talos.sv.symbol.type_parameter @U attributes {node_id = 13 : i64, hierarchical_name = "left.a.U", name = "U", semantic_type = !byte} {
              talos.sv.source.type_default attributes {node_id = 14 : i64, semantic_type = !int} {
              }
            }
          }
        }
        talos.sv.symbol.instance @b attributes {node_id = 15 : i64, hierarchical_name = "left.b", is_uninstantiated = false, name = "b", referenced_path = "child", referenced_symbol = @child} {
          talos.sv.symbol.instance_body @body attributes {node_id = 16 : i64, hierarchical_name = "left.b", name = "child"} {
            talos.sv.symbol.parameter @N attributes {node_id = 17 : i64, hierarchical_name = "left.b.N", name = "N", semantic_type = !int, constant_value = "2"} {
              talos.sv.source.value_default attributes {node_id = 18 : i64} {
                talos.sv.expression.integer_literal attributes {node_id = 19 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {
                }
              }
            }
            talos.sv.symbol.type_parameter @U attributes {node_id = 20 : i64, hierarchical_name = "left.b.U", name = "U", semantic_type = !int} {
              talos.sv.source.type_default attributes {node_id = 21 : i64, semantic_type = !int} {
              }
            }
          }
        }
        talos.sv.symbol.instance @c attributes {node_id = 22 : i64, hierarchical_name = "left.c", is_uninstantiated = false, name = "c", referenced_path = "child", referenced_symbol = @child} {
          talos.sv.symbol.instance_body @body attributes {node_id = 23 : i64, hierarchical_name = "left.c", name = "child"} {
            talos.sv.symbol.parameter @N attributes {node_id = 24 : i64, hierarchical_name = "left.c.N", name = "N", semantic_type = !int, constant_value = "3"} {
              talos.sv.source.value_default attributes {node_id = 25 : i64} {
                talos.sv.expression.integer_literal attributes {node_id = 26 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {
                }
              }
            }
            talos.sv.symbol.type_parameter @U attributes {node_id = 27 : i64, hierarchical_name = "left.c.U", name = "U", semantic_type = !int} {
              talos.sv.source.type_default attributes {node_id = 28 : i64, semantic_type = !int} {
              }
            }
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 29 : i64, parameter = @root::@left::@left_body::@a::@body::@N, ordinal = 0 : i64} {
          talos.sv.expression.integer_literal attributes {node_id = 30 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 31 : i64, parameter = @root::@left::@left_body::@b::@body::@N, ordinal = 1 : i64, is_named = unit} {
          talos.sv.source.parameter_reference attributes {node_id = 32 : i64, parameter = @P} {
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 33 : i64, parameter = @root::@left::@left_body::@a::@body::@U, ordinal = 2 : i64, is_named = unit} {
          talos.sv.source.type_default attributes {node_id = 34 : i64, semantic_type = !byte} {
          } loc("override.sv":15:9)
        }
        talos.sv.source.parameter_actual attributes {node_id = 35 : i64, parameter = @root::@left::@left_body::@b::@body::@U, ordinal = 3 : i64} {
          talos.sv.source.type_reference_default attributes {node_id = 36 : i64, parameter = @T} {
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 37 : i64, parameter = @root::@left::@left_body::@c::@body::@N, ordinal = 4 : i64, is_named = unit, unavailable_rhs = "unsupported(P)"} {
        }
        talos.sv.source.parameter_actual attributes {node_id = 38 : i64, parameter = @root::@left::@left_body::@c::@body::@U, ordinal = 5 : i64, is_named = unit, use_default = unit} {
        }
      }
    }
    talos.sv.symbol.instance @right attributes {node_id = 39 : i64, hierarchical_name = "right", is_uninstantiated = false, name = "right", referenced_path = "parent", referenced_symbol = @parent} {
      talos.sv.symbol.instance_body @right_body attributes {node_id = 40 : i64, hierarchical_name = "right", name = "parent"} {
        talos.sv.symbol.parameter @P attributes {node_id = 41 : i64, hierarchical_name = "right.P", name = "P", semantic_type = !int, constant_value = "5"} {
        }
        talos.sv.symbol.type_parameter @T attributes {node_id = 42 : i64, hierarchical_name = "right.T", name = "T", semantic_type = !bit} {
        }
        talos.sv.symbol.instance @a attributes {node_id = 43 : i64, hierarchical_name = "right.a", is_uninstantiated = false, name = "a", referenced_path = "child", referenced_symbol = @child} {
          talos.sv.symbol.instance_body @body attributes {node_id = 44 : i64, hierarchical_name = "right.a", name = "child"} {
            talos.sv.symbol.parameter @N attributes {node_id = 45 : i64, hierarchical_name = "right.a.N", name = "N", semantic_type = !int, constant_value = "7"} {
              talos.sv.source.value_default attributes {node_id = 46 : i64} {
                talos.sv.expression.integer_literal attributes {node_id = 47 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {
                }
              }
            }
            talos.sv.symbol.type_parameter @U attributes {node_id = 48 : i64, hierarchical_name = "right.a.U", name = "U", semantic_type = !byte} {
              talos.sv.source.type_default attributes {node_id = 49 : i64, semantic_type = !int} {
              }
            }
          }
        }
        talos.sv.symbol.instance @b attributes {node_id = 50 : i64, hierarchical_name = "right.b", is_uninstantiated = false, name = "b", referenced_path = "child", referenced_symbol = @child} {
          talos.sv.symbol.instance_body @body attributes {node_id = 51 : i64, hierarchical_name = "right.b", name = "child"} {
            talos.sv.symbol.parameter @N attributes {node_id = 52 : i64, hierarchical_name = "right.b.N", name = "N", semantic_type = !int, constant_value = "5"} {
              talos.sv.source.value_default attributes {node_id = 53 : i64} {
                talos.sv.expression.integer_literal attributes {node_id = 54 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {
                }
              }
            }
            talos.sv.symbol.type_parameter @U attributes {node_id = 55 : i64, hierarchical_name = "right.b.U", name = "U", semantic_type = !bit} {
              talos.sv.source.type_default attributes {node_id = 56 : i64, semantic_type = !int} {
              }
            }
          }
        }
        talos.sv.symbol.instance @c attributes {node_id = 57 : i64, hierarchical_name = "right.c", is_uninstantiated = false, name = "c", referenced_path = "child", referenced_symbol = @child} {
          talos.sv.symbol.instance_body @body attributes {node_id = 58 : i64, hierarchical_name = "right.c", name = "child"} {
            talos.sv.symbol.parameter @N attributes {node_id = 59 : i64, hierarchical_name = "right.c.N", name = "N", semantic_type = !int, constant_value = "3"} {
              talos.sv.source.value_default attributes {node_id = 60 : i64} {
                talos.sv.expression.integer_literal attributes {node_id = 61 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {
                }
              }
            }
            talos.sv.symbol.type_parameter @U attributes {node_id = 62 : i64, hierarchical_name = "right.c.U", name = "U", semantic_type = !int} {
              talos.sv.source.type_default attributes {node_id = 63 : i64, semantic_type = !int} {
              }
            }
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 64 : i64, parameter = @root::@right::@right_body::@a::@body::@N, ordinal = 0 : i64} {
          talos.sv.expression.integer_literal attributes {node_id = 65 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 66 : i64, parameter = @root::@right::@right_body::@b::@body::@N, ordinal = 1 : i64, is_named = unit} {
          talos.sv.source.parameter_reference attributes {node_id = 67 : i64, parameter = @P} {
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 68 : i64, parameter = @root::@right::@right_body::@a::@body::@U, ordinal = 2 : i64, is_named = unit} {
          talos.sv.source.type_default attributes {node_id = 69 : i64, semantic_type = !byte} {
          } loc("override.sv":15:9)
        }
        talos.sv.source.parameter_actual attributes {node_id = 70 : i64, parameter = @root::@right::@right_body::@b::@body::@U, ordinal = 3 : i64} {
          talos.sv.source.type_reference_default attributes {node_id = 71 : i64, parameter = @T} {
          }
        }
        talos.sv.source.parameter_actual attributes {node_id = 72 : i64, parameter = @root::@right::@right_body::@c::@body::@N, ordinal = 4 : i64, is_named = unit, unavailable_rhs = "unsupported(P)"} {
        }
        talos.sv.source.parameter_actual attributes {node_id = 73 : i64, parameter = @root::@right::@right_body::@c::@body::@U, ordinal = 5 : i64, is_named = unit, use_default = unit} {
        }
      }
    }
  }
}
