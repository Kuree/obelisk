// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "covergroup_unsupported", name = "covergroup_unsupported", node_id = 0 : i64, sym_name = "s0.covergroup_unsupported"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "covergroup_owner", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "covergroup_owner", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.covergroup_owner>, sym_name = "s3.covergroup_owner", this_variable_path = "covergroup_owner::this", this_variable_symbol = @s1.$root::@s2::@s3.covergroup_owner::@s47.this} {
        obelisk.sv.type.covergroup_type attributes {constructor_argument_count = 0 : i64, has_coverage_event = false, hierarchical_name = "covergroup_owner", node_id = 4 : i64, sample_formal_count = 0 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.covergroup_owner::@s4>, sym_name = "s4"} {
          obelisk.sv.symbol.covergroup_body attributes {hierarchical_name = "covergroup_owner", node_id = 5 : i64, option_count = 0 : i64, sym_name = "s5"} {
            obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_owner.option", is_compiler_generated, name = "option", node_id = 6 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_owner", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s6.option"} {
            }
            obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_owner.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 7 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_owner", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s7.type_option"} {
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.set_inst_name", is_builtin, name = "set_inst_name", node_id = 8 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.set_inst_name", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "covergroup_owner.set_inst_name.name", name = "name", node_id = 10 : i64, semantic_type = !obelisk.string, sym_name = "s9.name"} {
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 11 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.get_coverage.covered_bins", name = "covered_bins", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.covered_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.get_coverage.total_bins", name = "total_bins", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.total_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 17 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s13.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 18 : i64} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.covered_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.get_inst_coverage.total_bins", name = "total_bins", node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.total_bins"} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.start", is_builtin, name = "start", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s16.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 24 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.stop", is_builtin, name = "stop", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 26 : i64} {
              }
            }
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.sample", is_builtin, name = "sample", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.sample", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 28 : i64} {
              }
            }
            obelisk.sv.symbol.coverpoint attributes {has_iff = false, hierarchical_name = "covergroup_owner.cp", name = "cp", node_id = 29 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s19.cp"} {
              obelisk.sv.expression.conversion attributes {node_id = 30 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_owner.cp.option", is_compiler_generated, name = "option", node_id = 32 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_owner.cp", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s20.option"} {
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_owner.cp.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 33 : i64, semantic_type = !obelisk.source_aggregate<"covergroup_owner.cp", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s21.type_option"} {
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.cp.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 34 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s22.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 35 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.cp.get_coverage.covered_bins", name = "covered_bins", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.cp.get_coverage.total_bins", name = "total_bins", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.cp.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 40 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s25.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 41 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.cp.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s26.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "covergroup_owner.cp.get_inst_coverage.total_bins", name = "total_bins", node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s27.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.cp.start", is_builtin, name = "start", node_id = 46 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 47 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner.cp.stop", is_builtin, name = "stop", node_id = 48 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s29.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 49 : i64} {
                }
              }
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "covergroup_owner.cp.one", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "one", node_id = 50 : i64, sym_name = "s30.one", transition_set_count = 0 : i64, value_count = 1 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 51 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "covergroup_owner::cg", is_const, name = "cg", node_id = 53 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.covergroup_owner::@s4>, sym_name = "s31.cg"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 54 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s32.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 55 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::pre_randomize", is_builtin, name = "pre_randomize", node_id = 56 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 57 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::post_randomize", is_builtin, name = "post_randomize", node_id = 58 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s34.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 59 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::get_randstate", is_builtin, name = "get_randstate", node_id = 60 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s35.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 61 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::set_randstate", is_builtin, name = "set_randstate", node_id = 62 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s36.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 63 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "covergroup_owner::set_randstate.state", name = "state", node_id = 64 : i64, semantic_type = !obelisk.string, sym_name = "s37.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::srandom", is_builtin, name = "srandom", node_id = 65 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s38.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 66 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "covergroup_owner::srandom.seed", name = "seed", node_id = 67 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s39.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::rand_mode", is_builtin, name = "rand_mode", node_id = 68 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s40.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 69 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "covergroup_owner::rand_mode.on_ff", name = "on_ff", node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s41.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "covergroup_owner::constraint_mode", is_builtin, name = "constraint_mode", node_id = 71 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s42.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 72 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "covergroup_owner::constraint_mode.on_ff", name = "on_ff", node_id = 73 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s43.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "covergroup_owner::this", is_compiler_generated, is_const, name = "this", node_id = 77 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.covergroup_owner>, sym_name = "s47.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "covergroup_unsupported", is_uninstantiated = false, name = "covergroup_unsupported", node_id = 74 : i64, referenced_path = "covergroup_unsupported", referenced_symbol = @s0.covergroup_unsupported, sym_name = "s44.covergroup_unsupported"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "covergroup_unsupported", name = "covergroup_unsupported", node_id = 75 : i64, sym_name = "s45.covergroup_unsupported"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "covergroup_unsupported.owner", lifetime = 1 : i32, name = "owner", node_id = 76 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.covergroup_owner>, sym_name = "s46.owner"} {
        }
      }
    }
  }
}

// CHECK: class-member and inherited covergroups are not executable
