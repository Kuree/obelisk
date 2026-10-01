// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/role-size.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=ROLE-SIZE
// RUN: not obelisk-opt %t/negative-value-count.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=NEGATIVE-VALUE-COUNT
// RUN: not obelisk-opt %t/empty-transition-set.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=EMPTY-TRANSITION-SET
// RUN: not obelisk-opt %t/oversized-transition-range.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=OVERSIZED-TRANSITION-RANGE
// RUN: not obelisk-opt %t/transition-topology.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=TOPOLOGY
// RUN: not obelisk-opt %t/repetition.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REPETITION

// ROLE-SIZE: malformed coverage-bin child inventory: child_roles size does not match the region
// NEGATIVE-VALUE-COUNT: malformed coverage-bin child inventory: value_count is out of bounds
// EMPTY-TRANSITION-SET: malformed coverage-bin child inventory: transition range count is out of bounds
// OVERSIZED-TRANSITION-RANGE: malformed coverage-bin child inventory: transition item count is out of bounds
// TOPOLOGY: malformed coverage-bin child inventory: transition-set count does not match its layout
// REPETITION: malformed coverage-bin child inventory: transition repetition metadata is invalid

//--- role-size.mlir
module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64} {
        obelisk.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 4 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>} {
          obelisk.sv.symbol.covergroup_body @cg_body attributes {hierarchical_name = "m.cg", node_id = 5 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 6 : i64, option_count = 0 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.symbol.coverage_bin @b attributes {bins_kind = 0 : i32, child_roles = array<i64>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.b", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "b", node_id = 8 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

//--- negative-value-count.mlir
module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64} {
        obelisk.sv.symbol.coverage_bin @b attributes {bins_kind = 0 : i32, child_roles = array<i64>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.b", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "b", node_id = 4 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = -1 : i64} {
        }
      }
    }
  }
}

//--- empty-transition-set.mlir
module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64} {
        obelisk.sv.symbol.coverage_bin @b attributes {bins_kind = 0 : i32, child_roles = array<i64>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.b", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "b", node_id = 4 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 1 : i64, transition_set_range_counts = array<i64: 0>, value_count = 0 : i64} {
        }
      }
    }
  }
}

//--- transition-topology.mlir
module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64} {
        obelisk.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 4 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>} {
          obelisk.sv.symbol.covergroup_body @cg_body attributes {hierarchical_name = "m.cg", node_id = 5 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 6 : i64, option_count = 0 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.symbol.coverage_bin @b attributes {bins_kind = 0 : i32, child_roles = array<i64: 6>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.b", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "b", node_id = 8 : i64, transition_range_has_repeat_from = array<i64: 0>, transition_range_has_repeat_to = array<i64: 0>, transition_range_item_counts = array<i64: 1>, transition_range_repeat_kinds = array<i64: 0>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64: 1>, value_count = 0 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

//--- oversized-transition-range.mlir
module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64} {
        obelisk.sv.symbol.coverage_bin @b attributes {bins_kind = 0 : i32, child_roles = array<i64>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.b", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "b", node_id = 4 : i64, transition_range_has_repeat_from = array<i64: 0>, transition_range_has_repeat_to = array<i64: 0>, transition_range_item_counts = array<i64: 9223372036854775807>, transition_range_repeat_kinds = array<i64: 0>, transition_set_count = 1 : i64, transition_set_range_counts = array<i64: 1>, value_count = 0 : i64} {
        }
      }
    }
  }
}

//--- repetition.mlir
module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64} {
        obelisk.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 4 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>} {
          obelisk.sv.symbol.covergroup_body @cg_body attributes {hierarchical_name = "m.cg", node_id = 5 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 6 : i64, option_count = 0 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.symbol.coverage_bin @b attributes {bins_kind = 0 : i32, child_roles = array<i64: 6>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.b", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "b", node_id = 8 : i64, transition_range_has_repeat_from = array<i64: 0>, transition_range_has_repeat_to = array<i64: 0>, transition_range_item_counts = array<i64: 1>, transition_range_repeat_kinds = array<i64: 1>, transition_set_count = 1 : i64, transition_set_range_counts = array<i64: 1>, value_count = 0 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
