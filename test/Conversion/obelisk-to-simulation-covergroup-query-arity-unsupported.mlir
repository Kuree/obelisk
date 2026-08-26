// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m, sym_name = "i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.type.covergroup_type attributes {constructor_argument_count = 0 : i64, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>, sym_name = "cg"} {
          obelisk.sv.symbol.covergroup_body attributes {hierarchical_name = "m.cg", node_id = 9 : i64, option_count = 0 : i64, sym_name = "s9"} {
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "m.cg.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            }
            obelisk.sv.symbol.coverpoint attributes {has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 33 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "cp"} {
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.one", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "one", node_id = 54 : i64, sym_name = "one", transition_set_count = 0 : i64, value_count = 1 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.c", lifetime = 1 : i32, name = "c", node_id = 57 : i64, semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>, sym_name = "c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 59 : i64, procedure_kind = 0 : i32, sym_name = "s37", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 60 : i64} {
            obelisk.sv.statement.list attributes {node_id = 61 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 67 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.m", system_scope_path = "m", system_scope_symbol = @root::@i::@body} {
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "get_inst_coverage", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 1>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 69 : i64, referenced_path = "m.cg.get_inst_coverage", referenced_symbol = @root::@i::@body::@cg::@s9::@get_inst_coverage, semantic_type = !obelisk.real, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 70 : i64, referenced_path = "m.c", referenced_symbol = @root::@i::@body::@c, semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>} {
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

// CHECK: coverage queries require either zero or two output arguments

