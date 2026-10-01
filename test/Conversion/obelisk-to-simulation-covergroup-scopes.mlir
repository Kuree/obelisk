// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.coverage_interface attributes {definition_kind = 1 : i32, hierarchical_name = "coverage_interface", name = "coverage_interface", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.coverage_module attributes {definition_kind = 0 : i32, hierarchical_name = "coverage_module", name = "coverage_module", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.definition @s2.coverage_program attributes {definition_kind = 2 : i32, hierarchical_name = "coverage_program", name = "coverage_program", node_id = 2 : i64} {
  }
  obelisk.sv.symbol.root @s3.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64} {
    obelisk.sv.symbol.compilation_unit @s4 attributes {hierarchical_name = "$unit", node_id = 4 : i64} {
    }
    obelisk.sv.symbol.instance @s5.coverage_module attributes {hierarchical_name = "coverage_module", is_uninstantiated = false, name = "coverage_module", node_id = 5 : i64, referenced_path = "coverage_module", referenced_symbol = @s1.coverage_module} {
      obelisk.sv.symbol.instance_body @s6.coverage_module attributes {hierarchical_name = "coverage_module", name = "coverage_module", node_id = 6 : i64} {
        obelisk.sv.symbol.variable @s7.value attributes {hierarchical_name = "coverage_module.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.instance @s8.intf attributes {hierarchical_name = "coverage_module.intf", is_uninstantiated = false, name = "intf", node_id = 8 : i64, referenced_path = "coverage_interface", referenced_symbol = @s0.coverage_interface} {
          obelisk.sv.symbol.instance_body @s9.coverage_interface attributes {hierarchical_name = "coverage_module.intf", name = "coverage_interface", node_id = 9 : i64} {
            obelisk.sv.symbol.variable @s10.value attributes {hierarchical_name = "coverage_module.intf.value", lifetime = 1 : i32, name = "value", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
            obelisk.sv.type.covergroup_type @s11.interface_group attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 2 : i32, has_coverage_event = false, hierarchical_name = "coverage_module.intf.interface_group", name = "interface_group", node_id = 11 : i64, sample_formal_count = 1 : i64, sample_formals = [@s12.sample_value], semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s9.coverage_interface::@s11.interface_group>} {
              obelisk.sv.symbol.formal_argument @s12.sample_value attributes {direction = 0 : i32, hierarchical_name = "coverage_module.intf.interface_group::sample_value", is_coverage_sample_formal, name = "sample_value", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.symbol.covergroup_body @s13 attributes {hierarchical_name = "coverage_module.intf.interface_group", node_id = 13 : i64, option_count = 0 : i64} {
                obelisk.sv.symbol.class_property @s14.option attributes {hierarchical_name = "coverage_module.intf.interface_group.option", is_compiler_generated, name = "option", node_id = 14 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.intf.interface_group", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
                obelisk.sv.symbol.class_property @s15.type_option attributes {hierarchical_name = "coverage_module.intf.interface_group.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 15 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.intf.interface_group", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
                }
                obelisk.sv.symbol.subroutine @s16.set_inst_name attributes {hierarchical_name = "coverage_module.intf.interface_group.set_inst_name", is_builtin, name = "set_inst_name", node_id = 16 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 17 : i64} {
                  }
                  obelisk.sv.symbol.formal_argument @s17.name attributes {direction = 0 : i32, hierarchical_name = "coverage_module.intf.interface_group.set_inst_name.name", name = "name", node_id = 18 : i64, semantic_type = !obelisk.string} {
                  }
                }
                obelisk.sv.symbol.subroutine @s18.get_coverage attributes {hierarchical_name = "coverage_module.intf.interface_group.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 19 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 20 : i64} {
                  }
                  obelisk.sv.symbol.formal_argument @s19.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.get_coverage.covered_bins", name = "covered_bins", node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.symbol.formal_argument @s20.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.get_coverage.total_bins", name = "total_bins", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.symbol.subroutine @s21.get_inst_coverage attributes {hierarchical_name = "coverage_module.intf.interface_group.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 25 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 26 : i64} {
                  }
                  obelisk.sv.symbol.formal_argument @s22.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.symbol.formal_argument @s23.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.get_inst_coverage.total_bins", name = "total_bins", node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.symbol.subroutine @s24.start attributes {hierarchical_name = "coverage_module.intf.interface_group.start", is_builtin, name = "start", node_id = 31 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 32 : i64} {
                  }
                }
                obelisk.sv.symbol.subroutine @s25.stop attributes {hierarchical_name = "coverage_module.intf.interface_group.stop", is_builtin, name = "stop", node_id = 33 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 34 : i64} {
                  }
                }
                obelisk.sv.symbol.subroutine @s26.sample attributes {hierarchical_name = "coverage_module.intf.interface_group.sample", is_builtin, name = "sample", node_id = 35 : i64, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 36 : i64} {
                  }
                  obelisk.sv.symbol.formal_argument @s27.sample_value attributes {direction = 0 : i32, hierarchical_name = "coverage_module.intf.interface_group.sample.sample_value", is_coverage_sample_formal, name = "sample_value", node_id = 37 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
                obelisk.sv.symbol.coverpoint @s28.cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "coverage_module.intf.interface_group.cp", name = "cp", node_id = 38 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "coverage_module.intf.interface_group::sample_value", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s8.intf::@s9.coverage_interface::@s11.interface_group::@s12.sample_value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.symbol.class_property @s29.option attributes {hierarchical_name = "coverage_module.intf.interface_group.cp.option", is_compiler_generated, name = "option", node_id = 40 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.intf.interface_group.cp", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  }
                  obelisk.sv.symbol.class_property @s30.type_option attributes {hierarchical_name = "coverage_module.intf.interface_group.cp.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 41 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.intf.interface_group.cp", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
                  }
                  obelisk.sv.symbol.subroutine @s31.get_coverage attributes {hierarchical_name = "coverage_module.intf.interface_group.cp.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                    obelisk.sv.statement.list attributes {node_id = 43 : i64} {
                    }
                    obelisk.sv.symbol.formal_argument @s32.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.cp.get_coverage.covered_bins", name = "covered_bins", node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.symbol.formal_argument @s33.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.cp.get_coverage.total_bins", name = "total_bins", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.symbol.subroutine @s34.get_inst_coverage attributes {hierarchical_name = "coverage_module.intf.interface_group.cp.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 48 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                    obelisk.sv.statement.list attributes {node_id = 49 : i64} {
                    }
                    obelisk.sv.symbol.formal_argument @s35.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.cp.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.symbol.formal_argument @s36.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.intf.interface_group.cp.get_inst_coverage.total_bins", name = "total_bins", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.symbol.subroutine @s37.start attributes {hierarchical_name = "coverage_module.intf.interface_group.cp.start", is_builtin, name = "start", node_id = 54 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                    obelisk.sv.statement.list attributes {node_id = 55 : i64} {
                    }
                  }
                  obelisk.sv.symbol.subroutine @s38.stop attributes {hierarchical_name = "coverage_module.intf.interface_group.cp.stop", is_builtin, name = "stop", node_id = 56 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                    obelisk.sv.statement.list attributes {node_id = 57 : i64} {
                    }
                  }
                  obelisk.sv.symbol.coverage_bin @s39.low attributes {bins_kind = 0 : i32, child_roles = array<i64: 5, 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "coverage_module.intf.interface_group.cp.low", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "low", node_id = 58 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 2 : i64} {
                    obelisk.sv.expression.conversion attributes {node_id = 59 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 60 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 63 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.symbol.coverage_bin @s40.high attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "coverage_module.intf.interface_group.cp.high", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "high", node_id = 65 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                    obelisk.sv.expression.value_range attributes {node_id = 66 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                      obelisk.sv.expression.conversion attributes {node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.conversion attributes {node_id = 68 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 69 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                      }
                      obelisk.sv.expression.conversion attributes {node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.conversion attributes {node_id = 71 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.symbol.variable @s41.c attributes {hierarchical_name = "coverage_module.intf.c", lifetime = 1 : i32, name = "c", node_id = 73 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s9.coverage_interface::@s11.interface_group>} {
            }
            obelisk.sv.symbol.procedural_block @s42 attributes {hierarchical_name = "coverage_module.intf", node_id = 74 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.block attributes {node_id = 75 : i64} {
                obelisk.sv.statement.list attributes {node_id = 76 : i64} {
                  obelisk.sv.statement.expression_statement attributes {node_id = 77 : i64} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 78 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s9.coverage_interface::@s11.interface_group>} {
                      obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "coverage_module.intf.c", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s8.intf::@s9.coverage_interface::@s41.c, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s9.coverage_interface::@s11.interface_group>} {
                      }
                      obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, node_id = 80 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s9.coverage_interface::@s11.interface_group>} {
                      }
                    }
                  }
                  obelisk.sv.statement.expression_statement attributes {node_id = 81 : i64} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 82 : i64, referenced_path = "coverage_module.intf.interface_group.sample", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s8.intf::@s9.coverage_interface::@s11.interface_group::@s13::@s26.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                      obelisk.sv.expression.named_value attributes {node_id = 83 : i64, referenced_path = "coverage_module.intf.c", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s8.intf::@s9.coverage_interface::@s41.c, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s9.coverage_interface::@s11.interface_group>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 84 : i64, referenced_path = "coverage_module.intf.value", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s8.intf::@s9.coverage_interface::@s10.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.type.covergroup_type @s43.module_group attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "coverage_module.module_group", name = "module_group", node_id = 85 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s43.module_group>} {
          obelisk.sv.symbol.covergroup_body @s44 attributes {hierarchical_name = "coverage_module.module_group", node_id = 86 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.class_property @s45.option attributes {hierarchical_name = "coverage_module.module_group.option", is_compiler_generated, name = "option", node_id = 87 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.module_group", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
            }
            obelisk.sv.symbol.class_property @s46.type_option attributes {hierarchical_name = "coverage_module.module_group.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 88 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.module_group", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
            }
            obelisk.sv.symbol.subroutine @s47.set_inst_name attributes {hierarchical_name = "coverage_module.module_group.set_inst_name", is_builtin, name = "set_inst_name", node_id = 89 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 90 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s48.name attributes {direction = 0 : i32, hierarchical_name = "coverage_module.module_group.set_inst_name.name", name = "name", node_id = 91 : i64, semantic_type = !obelisk.string} {
              }
            }
            obelisk.sv.symbol.subroutine @s49.get_coverage attributes {hierarchical_name = "coverage_module.module_group.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 92 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 93 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s50.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.get_coverage.covered_bins", name = "covered_bins", node_id = 94 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 95 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument @s51.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.get_coverage.total_bins", name = "total_bins", node_id = 96 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 97 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine @s52.get_inst_coverage attributes {hierarchical_name = "coverage_module.module_group.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 98 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 99 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s53.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 100 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 101 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument @s54.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.get_inst_coverage.total_bins", name = "total_bins", node_id = 102 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 103 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine @s55.start attributes {hierarchical_name = "coverage_module.module_group.start", is_builtin, name = "start", node_id = 104 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 105 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine @s56.stop attributes {hierarchical_name = "coverage_module.module_group.stop", is_builtin, name = "stop", node_id = 106 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 107 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine @s57.sample attributes {hierarchical_name = "coverage_module.module_group.sample", is_builtin, name = "sample", node_id = 108 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 109 : i64} {
              }
            }
            obelisk.sv.symbol.coverpoint @s58.cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "coverage_module.module_group.cp", name = "cp", node_id = 110 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 111 : i64, referenced_path = "coverage_module.value", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s7.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.symbol.class_property @s59.option attributes {hierarchical_name = "coverage_module.module_group.cp.option", is_compiler_generated, name = "option", node_id = 112 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.module_group.cp", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              }
              obelisk.sv.symbol.class_property @s60.type_option attributes {hierarchical_name = "coverage_module.module_group.cp.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 113 : i64, semantic_type = !obelisk.source_aggregate<"coverage_module.module_group.cp", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
              }
              obelisk.sv.symbol.subroutine @s61.get_coverage attributes {hierarchical_name = "coverage_module.module_group.cp.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 114 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 115 : i64} {
                }
                obelisk.sv.symbol.formal_argument @s62.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.cp.get_coverage.covered_bins", name = "covered_bins", node_id = 116 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 117 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument @s63.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.cp.get_coverage.total_bins", name = "total_bins", node_id = 118 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine @s64.get_inst_coverage attributes {hierarchical_name = "coverage_module.module_group.cp.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 120 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 121 : i64} {
                }
                obelisk.sv.symbol.formal_argument @s65.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.cp.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 122 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 123 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument @s66.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_module.module_group.cp.get_inst_coverage.total_bins", name = "total_bins", node_id = 124 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 125 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine @s67.start attributes {hierarchical_name = "coverage_module.module_group.cp.start", is_builtin, name = "start", node_id = 126 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 127 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine @s68.stop attributes {hierarchical_name = "coverage_module.module_group.cp.stop", is_builtin, name = "stop", node_id = 128 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 129 : i64} {
                }
              }
              obelisk.sv.symbol.coverage_bin @s69.clear attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "coverage_module.module_group.cp.clear", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "clear", node_id = 130 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 131 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 132 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 133 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.symbol.coverage_bin @s70.set attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "coverage_module.module_group.cp.set", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "set", node_id = 134 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 135 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 136 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 137 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s71.c attributes {hierarchical_name = "coverage_module.c", lifetime = 1 : i32, name = "c", node_id = 138 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s43.module_group>} {
        }
        obelisk.sv.symbol.procedural_block @s72 attributes {hierarchical_name = "coverage_module", node_id = 139 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 140 : i64} {
            obelisk.sv.statement.list attributes {node_id = 141 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 142 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 143 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s43.module_group>} {
                  obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "coverage_module.c", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s71.c, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s43.module_group>} {
                  }
                  obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, node_id = 145 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s43.module_group>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 146 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 147 : i64, referenced_path = "coverage_module.module_group.sample", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s43.module_group::@s44::@s57.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 148 : i64, referenced_path = "coverage_module.c", referenced_symbol = @s3.$root::@s5.coverage_module::@s6.coverage_module::@s71.c, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s6.coverage_module::@s43.module_group>} {
                  }
                }
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance @s73.coverage_program attributes {hierarchical_name = "coverage_program", is_uninstantiated = false, name = "coverage_program", node_id = 149 : i64, referenced_path = "coverage_program", referenced_symbol = @s2.coverage_program} {
      obelisk.sv.symbol.instance_body @s74.coverage_program attributes {hierarchical_name = "coverage_program", name = "coverage_program", node_id = 150 : i64} {
        obelisk.sv.symbol.variable @s75.value attributes {hierarchical_name = "coverage_program.value", lifetime = 1 : i32, name = "value", node_id = 151 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.type.covergroup_type @s76.program_group attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "coverage_program.program_group", name = "program_group", node_id = 152 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@s3.$root::@s74.coverage_program::@s76.program_group>} {
          obelisk.sv.symbol.covergroup_body @s77 attributes {hierarchical_name = "coverage_program.program_group", node_id = 153 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.class_property @s78.option attributes {hierarchical_name = "coverage_program.program_group.option", is_compiler_generated, name = "option", node_id = 154 : i64, semantic_type = !obelisk.source_aggregate<"coverage_program.program_group", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
            }
            obelisk.sv.symbol.class_property @s79.type_option attributes {hierarchical_name = "coverage_program.program_group.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 155 : i64, semantic_type = !obelisk.source_aggregate<"coverage_program.program_group", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
            }
            obelisk.sv.symbol.subroutine @s80.set_inst_name attributes {hierarchical_name = "coverage_program.program_group.set_inst_name", is_builtin, name = "set_inst_name", node_id = 156 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 157 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s81.name attributes {direction = 0 : i32, hierarchical_name = "coverage_program.program_group.set_inst_name.name", name = "name", node_id = 158 : i64, semantic_type = !obelisk.string} {
              }
            }
            obelisk.sv.symbol.subroutine @s82.get_coverage attributes {hierarchical_name = "coverage_program.program_group.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 159 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 160 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s83.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.get_coverage.covered_bins", name = "covered_bins", node_id = 161 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 162 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument @s84.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.get_coverage.total_bins", name = "total_bins", node_id = 163 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 164 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine @s85.get_inst_coverage attributes {hierarchical_name = "coverage_program.program_group.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 165 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 166 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s86.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 167 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 168 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument @s87.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.get_inst_coverage.total_bins", name = "total_bins", node_id = 169 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 170 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine @s88.start attributes {hierarchical_name = "coverage_program.program_group.start", is_builtin, name = "start", node_id = 171 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 172 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine @s89.stop attributes {hierarchical_name = "coverage_program.program_group.stop", is_builtin, name = "stop", node_id = 173 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 174 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine @s90.sample attributes {hierarchical_name = "coverage_program.program_group.sample", is_builtin, name = "sample", node_id = 175 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 176 : i64} {
              }
            }
            obelisk.sv.symbol.coverpoint @s91.cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "coverage_program.program_group.cp", name = "cp", node_id = 177 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 178 : i64, referenced_path = "coverage_program.value", referenced_symbol = @s3.$root::@s73.coverage_program::@s74.coverage_program::@s75.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.symbol.class_property @s92.option attributes {hierarchical_name = "coverage_program.program_group.cp.option", is_compiler_generated, name = "option", node_id = 179 : i64, semantic_type = !obelisk.source_aggregate<"coverage_program.program_group.cp", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              }
              obelisk.sv.symbol.class_property @s93.type_option attributes {hierarchical_name = "coverage_program.program_group.cp.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 180 : i64, semantic_type = !obelisk.source_aggregate<"coverage_program.program_group.cp", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>} {
              }
              obelisk.sv.symbol.subroutine @s94.get_coverage attributes {hierarchical_name = "coverage_program.program_group.cp.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 181 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 182 : i64} {
                }
                obelisk.sv.symbol.formal_argument @s95.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.cp.get_coverage.covered_bins", name = "covered_bins", node_id = 183 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 184 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument @s96.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.cp.get_coverage.total_bins", name = "total_bins", node_id = 185 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 186 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine @s97.get_inst_coverage attributes {hierarchical_name = "coverage_program.program_group.cp.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 187 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 188 : i64} {
                }
                obelisk.sv.symbol.formal_argument @s98.covered_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.cp.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 189 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 190 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument @s99.total_bins attributes {direction = 3 : i32, hierarchical_name = "coverage_program.program_group.cp.get_inst_coverage.total_bins", name = "total_bins", node_id = 191 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 192 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine @s100.start attributes {hierarchical_name = "coverage_program.program_group.cp.start", is_builtin, name = "start", node_id = 193 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 194 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine @s101.stop attributes {hierarchical_name = "coverage_program.program_group.cp.stop", is_builtin, name = "stop", node_id = 195 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 196 : i64} {
                }
              }
              obelisk.sv.symbol.coverage_bin @s102.clear attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "coverage_program.program_group.cp.clear", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "clear", node_id = 197 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 198 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 199 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 200 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.symbol.coverage_bin @s103.set attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "coverage_program.program_group.cp.set", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "set", node_id = 201 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 202 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 203 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 204 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s104.c attributes {hierarchical_name = "coverage_program.c", lifetime = 1 : i32, name = "c", node_id = 205 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s74.coverage_program::@s76.program_group>} {
        }
        obelisk.sv.symbol.procedural_block @s105 attributes {hierarchical_name = "coverage_program", node_id = 206 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 207 : i64} {
            obelisk.sv.statement.list attributes {node_id = 208 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 209 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 210 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s74.coverage_program::@s76.program_group>} {
                  obelisk.sv.expression.named_value attributes {node_id = 211 : i64, referenced_path = "coverage_program.c", referenced_symbol = @s3.$root::@s73.coverage_program::@s74.coverage_program::@s104.c, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s74.coverage_program::@s76.program_group>} {
                  }
                  obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, node_id = 212 : i64, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s74.coverage_program::@s76.program_group>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 213 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 214 : i64, referenced_path = "coverage_program.program_group.sample", referenced_symbol = @s3.$root::@s73.coverage_program::@s74.coverage_program::@s76.program_group::@s77::@s90.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 215 : i64, referenced_path = "coverage_program.c", referenced_symbol = @s3.$root::@s73.coverage_program::@s74.coverage_program::@s104.c, semantic_type = !obelisk.covergroup_handle<@s3.$root::@s74.coverage_program::@s76.program_group>} {
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

// CHECK-COUNT-3: simulation.covergroup.decl
// CHECK: simulation.covergroup.create
// CHECK: simulation.covergroup.sample_enabled
// CHECK: simulation.covergroup.sample
// CHECK-NOT: obelisk.sv.
