// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.GenerateParameters
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.GenerateParameters
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!index = !obelisk.integral<32, true, true, 31 : 0, integer>
!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top_def attributes {node_id = 1 : i64, hierarchical_name = "top_def", name = "top_def", definition_kind = 0 : i32} {}
  obelisk.sv.symbol.definition @bus_def attributes {node_id = 2 : i64, hierarchical_name = "bus_def", name = "bus_def", definition_kind = 1 : i32} {}
  obelisk.sv.symbol.root @root attributes {node_id = 3 : i64, hierarchical_name = "$root", name = "$root"} {
    obelisk.sv.symbol.instance @top attributes {node_id = 4 : i64, hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_symbol = @top_def, referenced_path = "top_def"} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 5 : i64, hierarchical_name = "top", name = "top_def"} {
        obelisk.sv.symbol.generate_block_array @g attributes {node_id = 6 : i64, hierarchical_name = "top.g", name = "g", array_indices = array<i64: -1, 2>} {
          obelisk.sv.symbol.generate_block @g_neg attributes {node_id = 7 : i64, hierarchical_name = "top.g[-1]", name = "g"} {
            obelisk.sv.symbol.parameter @idx attributes {node_id = 8 : i64, hierarchical_name = "top.g[-1].idx", name = "idx", constant_value = "-1", semantic_type = !index, is_local_param} {}
            obelisk.sv.symbol.parameter @K attributes {node_id = 9 : i64, hierarchical_name = "top.g[-1].K", name = "K", constant_value = "42", semantic_type = !int, is_local_param} {
              obelisk.sv.source.value_default attributes {node_id = 10 : i64} {
                obelisk.sv.expression.integer_literal attributes {node_id = 11 : i64, constant_value = "42", is_signed = true, semantic_type = !int} {}
              }
            }
            obelisk.sv.symbol.parameter @L attributes {node_id = 12 : i64, hierarchical_name = "top.g[-1].L.p", name = "L.p", constant_value = "8'hfe", semantic_type = !obelisk.ranged_packed_array<7 : 0 x !bit>, is_local_param, has_explicit_range} {}
            obelisk.sv.symbol.parameter @Q attributes {node_id = 13 : i64, hierarchical_name = "top.g[-1].Q", name = "Q", constant_value = "42", semantic_type = !int, is_local_param} {
              obelisk.sv.source.value_default attributes {node_id = 14 : i64} {
                obelisk.sv.source.parameter_reference attributes {node_id = 15 : i64, parameter = @K} {}
              }
            }
            obelisk.sv.symbol.generate_block @inner attributes {node_id = 16 : i64, hierarchical_name = "top.g[-1].inner", name = "inner"} {
              obelisk.sv.symbol.parameter @K attributes {node_id = 17 : i64, hierarchical_name = "top.g[-1].inner.K", name = "K", constant_value = "42", semantic_type = !int, is_local_param} {}
            }
          }
          obelisk.sv.symbol.generate_block @g_pos attributes {node_id = 18 : i64, hierarchical_name = "top.g[2]", name = "g"} {
            obelisk.sv.symbol.parameter @idx attributes {node_id = 19 : i64, hierarchical_name = "top.g[2].idx", name = "idx", constant_value = "2", semantic_type = !index, is_local_param} {}
            obelisk.sv.symbol.parameter @K attributes {node_id = 20 : i64, hierarchical_name = "top.g[2].K", name = "K", constant_value = "42", semantic_type = !int, is_local_param} {
              obelisk.sv.source.value_default attributes {node_id = 21 : i64} {
                obelisk.sv.expression.integer_literal attributes {node_id = 22 : i64, constant_value = "42", is_signed = true, semantic_type = !int} {}
              }
            }
            obelisk.sv.symbol.parameter @L attributes {node_id = 23 : i64, hierarchical_name = "top.g[2].L.p", name = "L.p", constant_value = "8'hfe", semantic_type = !obelisk.ranged_packed_array<7 : 0 x !bit>, is_local_param, has_explicit_range} {}
            obelisk.sv.symbol.parameter @Q attributes {node_id = 24 : i64, hierarchical_name = "top.g[2].Q", name = "Q", constant_value = "42", semantic_type = !int, is_local_param} {
              obelisk.sv.source.value_default attributes {node_id = 25 : i64} {
                obelisk.sv.source.parameter_reference attributes {node_id = 26 : i64, parameter = @K} {}
              }
            }
          }
        }
        obelisk.sv.symbol.generate_block @inactive attributes {node_id = 27 : i64, hierarchical_name = "top.inactive", name = "inactive", is_uninstantiated = true} {
          obelisk.sv.symbol.parameter @hidden attributes {node_id = 28 : i64, hierarchical_name = "top.inactive.hidden", name = "hidden", constant_value = "123", semantic_type = !int, is_local_param} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {node_id = 29 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.conditional attributes {node_id = 30 : i64, condition_count = 1 : i64, has_else = false, check_kind = 0 : i32, condition_pattern_flags = array<i64: 0>} {
            obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_symbol = @root::@top::@body::@g::@g_neg::@idx, referenced_path = "top.g[-1].idx", semantic_type = !index} {}
            obelisk.sv.statement.empty attributes {node_id = 32 : i64} {}
          }
        }
      }
    }
    obelisk.sv.symbol.instance @bus attributes {node_id = 33 : i64, hierarchical_name = "bus", name = "bus", is_uninstantiated = false, referenced_symbol = @bus_def, referenced_path = "bus_def"} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 34 : i64, hierarchical_name = "bus", name = "bus_def"} {
        obelisk.sv.symbol.generate_block @chosen attributes {node_id = 35 : i64, hierarchical_name = "bus.chosen", name = "chosen"} {
          obelisk.sv.symbol.parameter @K attributes {node_id = 36 : i64, hierarchical_name = "bus.chosen.K", name = "K", constant_value = "42", semantic_type = !int, is_local_param} {}
        }
      }
    }
  }
}
