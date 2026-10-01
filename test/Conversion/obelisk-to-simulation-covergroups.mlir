// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.covergroup_lowering attributes {definition_kind = 0 : i32, hierarchical_name = "covergroup_lowering", name = "covergroup_lowering", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.covergroup_lowering attributes {hierarchical_name = "covergroup_lowering", is_uninstantiated = false, name = "covergroup_lowering", node_id = 3 : i64, referenced_path = "covergroup_lowering", referenced_symbol = @s0.covergroup_lowering} {
      obelisk.sv.symbol.instance_body @s4.covergroup_lowering attributes {hierarchical_name = "covergroup_lowering", name = "covergroup_lowering", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.enclosing attributes {hierarchical_name = "covergroup_lowering.enclosing", lifetime = 1 : i32, name = "enclosing", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.type.covergroup_type @s6.cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 2 : i32, has_coverage_event = false, hierarchical_name = "covergroup_lowering.cg", name = "cg", node_id = 6 : i64, sample_formal_count = 1 : i64, sample_formals = [@s7.sampled], semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
          obelisk.sv.symbol.formal_argument @s7.sampled attributes {direction = 0 : i32, hierarchical_name = "covergroup_lowering.cg::sampled", is_coverage_sample_formal, name = "sampled", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
          obelisk.sv.symbol.covergroup_body @s8 attributes {hierarchical_name = "covergroup_lowering.cg", node_id = 8 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.class_property @s9.option attributes {hierarchical_name = "covergroup_lowering.cg.option", is_compiler_generated, name = "option", node_id = 9 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_lowering.cg", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
            }
            obelisk.sv.symbol.class_property @s10.type_option attributes {hierarchical_name = "covergroup_lowering.cg.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 10 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_lowering.cg", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
            }
            obelisk.sv.symbol.subroutine @s11.set_inst_name attributes {hierarchical_name = "covergroup_lowering.cg.set_inst_name", is_builtin, name = "set_inst_name", node_id = 11 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s12.name attributes {direction = 0 : i32, hierarchical_name = "covergroup_lowering.cg.set_inst_name.name", name = "name", node_id = 13 : i64, semantic_type = !obelisk.string} {
              }
            }
            obelisk.sv.symbol.subroutine @s13.get_coverage attributes {hierarchical_name = "covergroup_lowering.cg.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 14 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 15 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s14.covered_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.get_coverage.covered_bins", name = "covered_bins", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument @s15.total_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.get_coverage.total_bins", name = "total_bins", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine @s16.get_inst_coverage attributes {hierarchical_name = "covergroup_lowering.cg.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 21 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s17.covered_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument @s18.total_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.get_inst_coverage.total_bins", name = "total_bins", node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine @s19.start attributes {hierarchical_name = "covergroup_lowering.cg.start", is_builtin, name = "start", node_id = 26 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 27 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine @s20.stop attributes {hierarchical_name = "covergroup_lowering.cg.stop", is_builtin, name = "stop", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 29 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine @s21.sample attributes {hierarchical_name = "covergroup_lowering.cg.sample", is_builtin, name = "sample", node_id = 30 : i64, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 31 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s22.sampled attributes {direction = 0 : i32, hierarchical_name = "covergroup_lowering.cg.sample.sampled", is_coverage_sample_formal, name = "sampled", node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
            obelisk.sv.symbol.coverpoint @s23.cp attributes {expression_roles = [0 : i32, 1 : i32], has_iff = true, hierarchical_name = "covergroup_lowering.cg.cp", name = "cp", node_id = 33 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "covergroup_lowering.cg::sampled", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s7.sampled, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.binary_op attributes {node_id = 35 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.conversion attributes {node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "covergroup_lowering.enclosing", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s5.enclosing, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 38 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.class_property @s24.option attributes {hierarchical_name = "covergroup_lowering.cg.cp.option", is_compiler_generated, name = "option", node_id = 40 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_lowering.cg.cp", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              }
              obelisk.sv.symbol.class_property @s25.type_option attributes {hierarchical_name = "covergroup_lowering.cg.cp.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 41 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_lowering.cg.cp", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
              }
              obelisk.sv.symbol.subroutine @s26.get_coverage attributes {hierarchical_name = "covergroup_lowering.cg.cp.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 43 : i64} {
                }
                obelisk.sv.symbol.formal_argument @s27.covered_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.cp.get_coverage.covered_bins", name = "covered_bins", node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument @s28.total_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.cp.get_coverage.total_bins", name = "total_bins", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine @s29.get_inst_coverage attributes {hierarchical_name = "covergroup_lowering.cg.cp.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 48 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 49 : i64} {
                }
                obelisk.sv.symbol.formal_argument @s30.covered_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.cp.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument @s31.total_bins attributes {direction = 3 : i32, hierarchical_name = "covergroup_lowering.cg.cp.get_inst_coverage.total_bins", name = "total_bins", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine @s32.start attributes {hierarchical_name = "covergroup_lowering.cg.cp.start", is_builtin, name = "start", node_id = 54 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 55 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine @s33.stop attributes {hierarchical_name = "covergroup_lowering.cg.cp.stop", is_builtin, name = "stop", node_id = 56 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 57 : i64} {
                }
              }
              obelisk.sv.symbol.coverage_bin @s34.values attributes {bins_kind = 0 : i32, child_roles = array<i64: 5, 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.cp.values", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "values", node_id = 58 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 2 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 59 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 60 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 63 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.symbol.coverage_bin @s35.range attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.cp.range", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "range", node_id = 65 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.value_range attributes {node_id = 66 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.conversion attributes {node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.conversion attributes {node_id = 68 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 69 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.conversion attributes {node_id = 71 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.symbol.coverage_bin @s36.fallback attributes {bins_kind = 0 : i32, child_roles = array<i64>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.cp.fallback", is_array = false, is_default = true, is_default_sequence = false, is_wildcard = false, name = "fallback", node_id = 73 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 0 : i64} {
              }
              obelisk.sv.symbol.coverage_bin @s41.removed attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.cp.removed", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "removed", node_id = 107 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 108 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.coverage_bin @s42.ignored attributes {bins_kind = 2 : i32, child_roles = array<i64: 5, 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.cp.ignored", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "ignored", node_id = 109 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 2 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 110 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 111 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.coverage_bin @s43.illegal attributes {bins_kind = 1 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.cp.illegal", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "illegal", node_id = 112 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 113 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.coverpoint @s44.empty attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "covergroup_lowering.cg.empty", name = "empty", node_id = 114 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "covergroup_lowering.cg::sampled", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s7.sampled, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.symbol.coverage_bin @s45.removed attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.empty.removed", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "removed", node_id = 116 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 117 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.coverage_bin @s46.ignored attributes {bins_kind = 2 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_lowering.cg.empty.ignored", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "ignored", node_id = 118 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s37.c attributes {hierarchical_name = "covergroup_lowering.c", lifetime = 1 : i32, name = "c", node_id = 74 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
        }
        obelisk.sv.symbol.variable @s38.covered attributes {hierarchical_name = "covergroup_lowering.covered", lifetime = 1 : i32, name = "covered", node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s39.total attributes {hierarchical_name = "covergroup_lowering.total", lifetime = 1 : i32, name = "total", node_id = 76 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s40 attributes {hierarchical_name = "covergroup_lowering", node_id = 77 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 78 : i64} {
            obelisk.sv.statement.list attributes {node_id = 79 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 81 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "covergroup_lowering.c", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s37.c, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  }
                  obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, node_id = 83 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 120 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "set_inst_name", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 121 : i64, referenced_path = "covergroup_lowering.cg.set_inst_name", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s8::@s11.set_inst_name, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 122 : i64, referenced_path = "covergroup_lowering.c", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s37.c, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "named", node_id = 123 : i64, semantic_type = !obelisk.string} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 84 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 85 : i64, referenced_path = "covergroup_lowering.cg.sample", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s8::@s21.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 86 : i64, referenced_path = "covergroup_lowering.c", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s37.c, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_path = "covergroup_lowering.enclosing", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s5.enclosing, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 88 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "stop", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 89 : i64, referenced_path = "covergroup_lowering.cg.stop", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s8::@s20.stop, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_path = "covergroup_lowering.c", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s37.c, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 91 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "start", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 92 : i64, referenced_path = "covergroup_lowering.cg.start", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s8::@s19.start, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 93 : i64, referenced_path = "covergroup_lowering.c", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s37.c, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 94 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 95 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.covergroup_lowering", system_scope_path = "covergroup_lowering", system_scope_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "%f", node_id = 96 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "get_inst_coverage", constraint_restrictions = [], defaulted_arguments = array<i64: 1, 1>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 97 : i64, referenced_path = "covergroup_lowering.cg.get_inst_coverage", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s8::@s16.get_inst_coverage, semantic_type = !obelisk.real, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 98 : i64, referenced_path = "covergroup_lowering.c", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s37.c, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_lowering::@s6.cg>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 99 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 100 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 101 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 102 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.covergroup_lowering", system_scope_path = "covergroup_lowering", system_scope_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "%f", node_id = 103 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "get_coverage", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 104 : i64, referenced_path = "covergroup_lowering.cg.get_coverage", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s6.cg::@s8::@s13.get_coverage, semantic_type = !obelisk.real, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 105 : i64, referenced_path = "covergroup_lowering.covered", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s38.covered, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 106 : i64, referenced_path = "covergroup_lowering.total", referenced_symbol = @s1.$root::@s3.covergroup_lowering::@s4.covergroup_lowering::@s39.total, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
// IEEE 1800-2017 19.5: the compiler evaluates each sampled expression once.
// The sample operation preserves typed values and schema identities; it does
// not encode bin decisions or table ordinals.
// CHECK: simulation.covergroup.decl @[[DECL:__obelisk_covergroup_.*]] schema {{[1-9][0-9]*}}
// CHECK: simulation.storage.decl {{[0-9]+}} in {{[0-9]+}} : !simulation.covergroup_handle<@[[DECL]]>
// CHECK: %[[HANDLE:.*]] = simulation.covergroup.create {{.*}} from @[[DECL]]
// CHECK: simulation.covergroup.set_name {{.*}} name %{{.*}}
// CHECK: simulation.covergroup.sample_enabled {{.*}}, %{{.*}}
// CHECK: simulation.covergroup.sample {{.*}} values[%{{.*}}, %{{.*}}, %{{.*}}] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// CHECK: simulation.covergroup.stop
// CHECK: simulation.covergroup.start
// CHECK: simulation.covergroup.instance_query
// CHECK: simulation.covergroup.type_query {{.*}} from @[[DECL]]
// CHECK: simulation.ref.store %covered
// CHECK: simulation.ref.store %total
// CHECK-NOT: obelisk.sv.
