// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "covergroup_unsupported", name = "covergroup_unsupported", node_id = 0 : i64, sym_name = "s0.covergroup_unsupported"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "covergroup_unsupported", is_uninstantiated = false, name = "covergroup_unsupported", node_id = 3 : i64, referenced_path = "covergroup_unsupported", referenced_symbol = @s0.covergroup_unsupported, sym_name = "s3.covergroup_unsupported"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "covergroup_unsupported", name = "covergroup_unsupported", node_id = 4 : i64, sym_name = "s4.covergroup_unsupported"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "covergroup_unsupported.clock", lifetime = 1 : i32, name = "clock", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clock"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "covergroup_unsupported.source", lifetime = 1 : i32, name = "source", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "covergroup_unsupported.real_value", lifetime = 1 : i32, name = "real_value", node_id = 7 : i64, semantic_type = !obelisk.real, sym_name = "s7.real_value"} {
        }
        obelisk.sv.type.covergroup_type attributes {constructor_argument_count = 0 : i64, has_coverage_event = false, hierarchical_name = "covergroup_unsupported.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.covergroup_unsupported::@s8.cg>, sym_name = "s8.cg"} {
          obelisk.sv.symbol.covergroup_body attributes {hierarchical_name = "covergroup_unsupported.cg", node_id = 9 : i64, option_count = 0 : i64, sym_name = "s9"} {
            obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.option", is_compiler_generated, name = "option", node_id = 10 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s10.option"} {
            }
            obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 11 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s11.type_option"} {
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.set_inst_name", is_builtin, name = "set_inst_name", node_id = 12 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.set_inst_name", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "covergroup_unsupported.cg.set_inst_name.name", name = "name", node_id = 14 : i64, semantic_type = !obelisk.string, sym_name = "s13.name"} {
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 15 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s14.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 16 : i64} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.get_coverage.covered_bins", name = "covered_bins", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.covered_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.get_coverage.total_bins", name = "total_bins", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.total_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s17.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 22 : i64} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s18.covered_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.get_inst_coverage.total_bins", name = "total_bins", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s19.total_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.start", is_builtin, name = "start", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s20.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 28 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.stop", is_builtin, name = "stop", node_id = 29 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s21.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 30 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.sample", is_builtin, name = "sample", node_id = 31 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s22.sample", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 32 : i64} {
              }
            }
            obelisk.sv.symbol.coverpoint attributes {has_iff = false, hierarchical_name = "covergroup_unsupported.cg.a", name = "a", node_id = 33 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s23.a"} {
              obelisk.sv.expression.conversion attributes {node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "covergroup_unsupported.source", referenced_symbol = @s1.$root::@s3.covergroup_unsupported::@s4.covergroup_unsupported::@s6.source, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.a.option", is_compiler_generated, name = "option", node_id = 36 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.a", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s24.option"} {
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.a.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 37 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.a", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s25.type_option"} {
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.a.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 38 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s26.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 39 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.a.get_coverage.covered_bins", name = "covered_bins", node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s27.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.a.get_coverage.total_bins", name = "total_bins", node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s28.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.a.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 44 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s29.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 45 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.a.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s30.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.a.get_inst_coverage.total_bins", name = "total_bins", node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s31.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.a.start", is_builtin, name = "start", node_id = 50 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s32.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 51 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.a.stop", is_builtin, name = "stop", node_id = 52 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 53 : i64} {
                }
              }
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_unsupported.cg.a.one", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "one", node_id = 54 : i64, sym_name = "s34.one", transition_set_count = 0 : i64, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 55 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 56 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.symbol.coverpoint attributes {has_iff = false, hierarchical_name = "covergroup_unsupported.cg.b", name = "b", node_id = 57 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s35.b"} {
              obelisk.sv.expression.conversion attributes {node_id = 58 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "covergroup_unsupported.source", referenced_symbol = @s1.$root::@s3.covergroup_unsupported::@s4.covergroup_unsupported::@s6.source, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.b.option", is_compiler_generated, name = "option", node_id = 60 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.b", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s36.option"} {
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.b.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 61 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.b", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s37.type_option"} {
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.b.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 62 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s38.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 63 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.b.get_coverage.covered_bins", name = "covered_bins", node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s39.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 65 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.b.get_coverage.total_bins", name = "total_bins", node_id = 66 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s40.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 67 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.b.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 68 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s41.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 69 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.b.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 70 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s42.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 71 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.b.get_inst_coverage.total_bins", name = "total_bins", node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s43.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 73 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.b.start", is_builtin, name = "start", node_id = 74 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s44.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 75 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.b.stop", is_builtin, name = "stop", node_id = 76 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s45.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 77 : i64} {
                }
              }
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_unsupported.cg.b.two", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "two", node_id = 78 : i64, sym_name = "s46.two", transition_set_count = 0 : i64, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 79 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 80 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.symbol.cover_cross attributes {hierarchical_name = "covergroup_unsupported.cg.ab", name = "ab", node_id = 81 : i64, sym_name = "s47.ab"} {
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.ab.option", is_compiler_generated, name = "option", node_id = 82 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.ab", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s48.option"} {
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_unsupported.cg.ab.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 83 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.ab", false, false, false, false, false, false, 0, 65, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}]>, sym_name = "s49.type_option"} {
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.ab.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 84 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s50.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 85 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.ab.get_coverage.covered_bins", name = "covered_bins", node_id = 86 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s51.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 87 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.ab.get_coverage.total_bins", name = "total_bins", node_id = 88 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s52.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 89 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.ab.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 90 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s53.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 91 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.ab.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 92 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s54.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 93 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_unsupported.cg.ab.get_inst_coverage.total_bins", name = "total_bins", node_id = 94 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s55.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 95 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.ab.start", is_builtin, name = "start", node_id = 96 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s56.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 97 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_unsupported.cg.ab.stop", is_builtin, name = "stop", node_id = 98 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s57.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 99 : i64} {
                }
              }
              obelisk.sv.symbol.cover_cross_body attributes {hierarchical_name = "covergroup_unsupported.cg.ab", node_id = 100 : i64, sym_name = "s58"} {
                obelisk.sv.type.type_alias attributes {hierarchical_name = "covergroup_unsupported.cg.ab.CrossValType", name = "CrossValType", node_id = 101 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_unsupported.cg.ab", false, false, false, false, false, false, 0, 64, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>}]>, sym_name = "s59.CrossValType"} {
                }
                obelisk.sv.type.type_alias attributes {hierarchical_name = "covergroup_unsupported.cg.ab.CrossQueueType", name = "CrossQueueType", node_id = 102 : i64, semantic_type = !obelisk.queue<!obelisk.source_aggregate<"covergroup_unsupported.cg.ab", false, false, false, false, false, false, 0, 64, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>}]>, 0>, sym_name = "s60.CrossQueueType"} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: coverage crosses are not supported
