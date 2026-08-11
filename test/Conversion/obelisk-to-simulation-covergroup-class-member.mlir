// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, constructor_path = "top.C::new", constructor_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s37.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "top.C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, sym_name = "s5.C", this_variable_path = "top.C::this", this_variable_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s76.this} {
          obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.C::value", name = "value", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.value"} {
          }
          obelisk.sv.type.covergroup_type attributes {constructor_argument_count = 0 : i64, has_coverage_event = false, hierarchical_name = "top.C", node_id = 7 : i64, sample_formal_count = 1 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, sym_name = "s7"} {
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C::extra", is_coverage_sample_formal, name = "extra", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.extra"} {
            }
            obelisk.sv.symbol.covergroup_body attributes {hierarchical_name = "top.C", node_id = 9 : i64, option_count = 0 : i64, sym_name = "s9"} {
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.C.option", is_compiler_generated, name = "option", node_id = 10 : i64, semantic_type = !obelisk.source_aggregate<"top.C", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s10.option"} {
              }
              obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.C.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 11 : i64, semantic_type = !obelisk.source_aggregate<"top.C", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s11.type_option"} {
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.set_inst_name", is_builtin, name = "set_inst_name", node_id = 12 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.set_inst_name", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 13 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C.set_inst_name.name", name = "name", node_id = 14 : i64, semantic_type = !obelisk.string, sym_name = "s13.name"} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 15 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s14.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 16 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.get_coverage.covered_bins", name = "covered_bins", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.get_coverage.total_bins", name = "total_bins", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s17.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 22 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s18.covered_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.get_inst_coverage.total_bins", name = "total_bins", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s19.total_bins"} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.start", is_builtin, name = "start", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s20.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 28 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.stop", is_builtin, name = "stop", node_id = 29 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s21.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 30 : i64} {
                }
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.sample", is_builtin, name = "sample", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s22.sample", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 32 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C.sample.extra", is_coverage_sample_formal, name = "extra", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.extra"} {
                }
              }
              obelisk.sv.symbol.coverpoint attributes {has_iff = false, hierarchical_name = "top.C.cp", name = "cp", node_id = 34 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s24.cp"} {
                obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 35 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 22, "../../../../tmp/classcg.sv", 5, 35, "">} {
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 22, "../../../../tmp/classcg.sv", 5, 27, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 37 : i64, referenced_path = "top.C::value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s6.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 22, "../../../../tmp/classcg.sv", 5, 27, "">} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 38 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 30, "../../../../tmp/classcg.sv", 5, 35, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 39 : i64, referenced_path = "top.C::extra", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s7::@s8.extra, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 30, "../../../../tmp/classcg.sv", 5, 35, "">} {
                    }
                  }
                }
                obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.C.cp.option", is_compiler_generated, name = "option", node_id = 40 : i64, semantic_type = !obelisk.source_aggregate<"top.C.cp", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s25.option"} {
                }
                obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.C.cp.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 41 : i64, semantic_type = !obelisk.source_aggregate<"top.C.cp", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, type = !obelisk.real}]>, sym_name = "s26.type_option"} {
                }
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.cp.get_coverage", is_builtin, is_static, name = "get_coverage", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s27.get_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 43 : i64} {
                  }
                  obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.cp.get_coverage.covered_bins", name = "covered_bins", node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s28.covered_bins"} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.cp.get_coverage.total_bins", name = "total_bins", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s29.total_bins"} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.cp.get_inst_coverage", is_builtin, name = "get_inst_coverage", node_id = 48 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s30.get_inst_coverage", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 49 : i64} {
                  }
                  obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.cp.get_inst_coverage.covered_bins", name = "covered_bins", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s31.covered_bins"} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.C.cp.get_inst_coverage.total_bins", name = "total_bins", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s32.total_bins"} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.cp.start", is_builtin, name = "start", node_id = 54 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.start", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 55 : i64} {
                  }
                }
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C.cp.stop", is_builtin, name = "stop", node_id = 56 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s34.stop", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 57 : i64} {
                  }
                }
                obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "top.C.cp.one", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "one", node_id = 58 : i64, sym_name = "s35.one", transition_set_count = 0 : i64, value_count = 1 : i64} {
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 59 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 50, "../../../../tmp/classcg.sv", 5, 51, "">} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 5, 50, "../../../../tmp/classcg.sv", 5, 51, "">} {
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.C::cg", is_const, name = "cg", node_id = 61 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, sym_name = "s36.cg"} {
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::new", is_constructor, name = "new", node_id = 62 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s37.new", this_variable_path = "top.C::new.this", this_variable_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s37.new::@s38.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 8, 7, "../../../../tmp/classcg.sv", 8, 16, "">} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 64 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 8, 7, "../../../../tmp/classcg.sv", 8, 15, "">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.C::cg", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s36.cg, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 8, 7, "../../../../tmp/classcg.sv", 8, 9, "">} {
                }
                obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, is_signed = false, node_id = 66 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 8, 12, "../../../../tmp/classcg.sv", 8, 15, "">} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.C::new.this", is_compiler_generated, is_const, name = "this", node_id = 67 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, sym_name = "s38.this"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::sample_it", name = "sample_it", node_id = 68 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s39.sample_it", this_variable_path = "top.C::sample_it.this", this_variable_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s39.sample_it::@s40.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 69 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 11, 7, "../../../../tmp/classcg.sv", 11, 20, "">} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 70 : i64, referenced_path = "top.C.sample", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s7::@s9::@s22.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 71 : i64, referenced_path = "top.C::cg", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s36.cg, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 11, 7, "../../../../tmp/classcg.sv", 11, 9, "">} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 11, 17, "../../../../tmp/classcg.sv", 11, 18, "">} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.C::sample_it.this", is_compiler_generated, is_const, name = "this", node_id = 73 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, sym_name = "s40.this"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 74 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s41.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 75 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 76 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s42.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 77 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::post_randomize", is_builtin, name = "post_randomize", node_id = 78 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s43.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 79 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::get_randstate", is_builtin, name = "get_randstate", node_id = 80 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s44.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 81 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::set_randstate", is_builtin, name = "set_randstate", node_id = 82 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s45.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 83 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C::set_randstate.state", name = "state", node_id = 84 : i64, semantic_type = !obelisk.string, sym_name = "s46.state"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::srandom", is_builtin, name = "srandom", node_id = 85 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s47.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 86 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C::srandom.seed", name = "seed", node_id = 87 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s48.seed"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::rand_mode", is_builtin, name = "rand_mode", node_id = 88 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s49.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 89 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C::rand_mode.on_ff", name = "on_ff", node_id = 90 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s50.on_ff"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 91 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s51.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 92 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.C::constraint_mode.on_ff", name = "on_ff", node_id = 93 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s52.on_ff"} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.C::this", is_compiler_generated, is_const, name = "this", node_id = 154 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, sym_name = "s76.this"} {
          }
        }
        obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "top.D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 94 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, sym_name = "s53.D", this_variable_path = "top.D::this", this_variable_symbol = @s1.$root::@s3.top::@s4.top::@s53.D::@s75.this} {
          obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.D::sample_it", name = "sample_it", node_id = 95 : i64, sym_name = "s54.sample_it"} {
          }
          obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.D::cg", name = "cg", node_id = 96 : i64, sym_name = "s55.cg"} {
          }
          obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.D::value", name = "value", node_id = 97 : i64, sym_name = "s56.value"} {
          }
          obelisk.sv.symbol.class_property attributes {hierarchical_name = "top.D::bias", name = "bias", node_id = 98 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s57.bias"} {
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::sample_other", name = "sample_other", node_id = 99 : i64, semantic_type = !obelisk.subroutine<(!obelisk.class_handle<@s1.$root::@s4.top::@s5.C>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s58.sample_other", this_variable_path = "top.D::sample_other.this", this_variable_symbol = @s1.$root::@s3.top::@s4.top::@s53.D::@s58.sample_other::@s60.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 100 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 17, 7, "../../../../tmp/classcg.sv", 17, 29, "">} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 101 : i64, referenced_path = "top.C.sample", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s7::@s9::@s22.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.member_access attributes {field_ordinal = 4 : i64, is_signed = false, node_id = 102 : i64, packed_offset = 0 : i64, referenced_path = "top.C::cg", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s36.cg, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 17, 7, "../../../../tmp/classcg.sv", 17, 15, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 103 : i64, referenced_path = "top.D::sample_other.other", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s53.D::@s58.sample_other::@s59.other, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 17, 7, "../../../../tmp/classcg.sv", 17, 12, "">} {
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 104 : i64, referenced_path = "top.D::bias", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s53.D::@s57.bias, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 17, 23, "../../../../tmp/classcg.sv", 17, 27, "">} {
                }
              }
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.D::sample_other.other", name = "other", node_id = 105 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, sym_name = "s59.other"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.D::sample_other.this", is_compiler_generated, is_const, name = "this", node_id = 106 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, sym_name = "s60.this"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 107 : i64, override_path = "top.C::randomize", override_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s41.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s61.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 108 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::pre_randomize", is_builtin, name = "pre_randomize", node_id = 109 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s62.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 110 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::post_randomize", is_builtin, name = "post_randomize", node_id = 111 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s63.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 112 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::get_randstate", is_builtin, name = "get_randstate", node_id = 113 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s64.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 114 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::set_randstate", is_builtin, name = "set_randstate", node_id = 115 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s65.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 116 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.D::set_randstate.state", name = "state", node_id = 117 : i64, semantic_type = !obelisk.string, sym_name = "s66.state"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::srandom", is_builtin, name = "srandom", node_id = 118 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s67.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 119 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.D::srandom.seed", name = "seed", node_id = 120 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s68.seed"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::rand_mode", is_builtin, name = "rand_mode", node_id = 121 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s69.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 122 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.D::rand_mode.on_ff", name = "on_ff", node_id = 123 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s70.on_ff"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.D::constraint_mode", is_builtin, name = "constraint_mode", node_id = 124 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s71.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 125 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.D::constraint_mode.on_ff", name = "on_ff", node_id = 126 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s72.on_ff"} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.D::this", is_compiler_generated, is_const, name = "this", node_id = 153 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, sym_name = "s75.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 127 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, sym_name = "s73.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 128 : i64, procedure_kind = 0 : i32, sym_name = "s74", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 129 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 21, 11, "../../../../tmp/classcg.sv", 27, 6, "">} {
            obelisk.sv.statement.list attributes {node_id = 130 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 21, 11, "../../../../tmp/classcg.sv", 27, 6, "">} {
              obelisk.sv.statement.expression_statement attributes {node_id = 131 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 22, 5, "../../../../tmp/classcg.sv", 22, 13, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 132 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 22, 5, "../../../../tmp/classcg.sv", 22, 12, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 133 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s73.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 22, 5, "../../../../tmp/classcg.sv", 22, 6, "">} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 134 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 22, 9, "../../../../tmp/classcg.sv", 22, 12, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 135 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 23, 5, "../../../../tmp/classcg.sv", 23, 17, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 136 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 23, 5, "../../../../tmp/classcg.sv", 23, 16, "">} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 90 : i64, is_signed = true, node_id = 137 : i64, packed_offset = 0 : i64, referenced_path = "top.C::value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s6.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 23, 5, "../../../../tmp/classcg.sv", 23, 12, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 138 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s73.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 23, 5, "../../../../tmp/classcg.sv", 23, 6, "">} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 139 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 23, 15, "../../../../tmp/classcg.sv", 23, 16, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 140 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 24, 5, "../../../../tmp/classcg.sv", 24, 19, "">} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "sample_it", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 141 : i64, referenced_path = "top.C::sample_it", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s39.sample_it, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 142 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s73.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 24, 5, "../../../../tmp/classcg.sv", 24, 6, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 143 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 25, 5, "../../../../tmp/classcg.sv", 25, 20, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 144 : i64, referenced_path = "top.C.sample", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s7::@s9::@s22.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 4 : i64, is_signed = false, node_id = 145 : i64, packed_offset = 0 : i64, referenced_path = "top.C::cg", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.C::@s36.cg, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.top::@s5.C::@s7>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 25, 5, "../../../../tmp/classcg.sv", 25, 9, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 146 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s73.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 25, 5, "../../../../tmp/classcg.sv", 25, 6, "">} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 147 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 25, 17, "../../../../tmp/classcg.sv", 25, 18, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 148 : i64, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 26, 5, "../../../../tmp/classcg.sv", 26, 23, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample_other", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 149 : i64, referenced_path = "top.D::sample_other", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s53.D::@s58.sample_other, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 150 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s73.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 26, 5, "../../../../tmp/classcg.sv", 26, 6, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 151 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s5.C>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 26, 20, "../../../../tmp/classcg.sv", 26, 21, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 152 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s73.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.top::@s53.D>, source_range = !obelisk.source_range<"../../../../tmp/classcg.sv", 26, 20, "../../../../tmp/classcg.sv", 26, 21, "">} {
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


// CHECK: obelisk_sim.covergroup.decl
// CHECK: obelisk_sim.class.decl @[[BASE:[^ ]+]] id 1
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "value"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "cg"
// CHECK: obelisk_sim.class.decl @[[DERIVED:[^ ]+]] id 2 extends @[[BASE]]
// CHECK: obelisk_sim.class.field {{.*}}of @[[DERIVED]]{{.*}}debug_name = "bias"
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk_sim.covergroup.create
// CHECK: obelisk_sim.class.field_ref %arg1[{{.*}}field_1]
// CHECK: obelisk_sim.managed.store
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: obelisk_sim.class.field_ref %arg1[{{.*}}field_1]
// CHECK: obelisk_sim.covergroup.sample_enabled
// CHECK: obelisk_sim.class.field_ref %arg1[{{.*}}field_0]
// CHECK: obelisk_sim.covergroup.sample
// CHECK-LABEL: obelisk_sim.func private @unit_2
// CHECK: obelisk_sim.class.field_ref %arg2[{{.*}}field_1]
// CHECK: obelisk_sim.class.field_ref %arg1[{{.*}}field_0]
// CHECK: obelisk_sim.covergroup.sample_enabled
// CHECK: obelisk_sim.class.field_ref %arg2[{{.*}}field_0]
// CHECK: obelisk_sim.covergroup.sample
// CHECK-LABEL: obelisk_sim.func private @unit_3
// CHECK: obelisk_sim.class.direct_call @unit_1
// CHECK: %[[OWNER:[0-9]+]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.class.field_ref %[[OWNER]][{{.*}}field_1] : !obelisk_sim.class_handle<@{{.*}}D>
// CHECK: obelisk_sim.covergroup.sample_enabled
// CHECK: obelisk_sim.class.field_ref %[[OWNER]][{{.*}}field_0] : !obelisk_sim.class_handle<@{{.*}}D>
// CHECK: obelisk_sim.covergroup.sample
// CHECK-NOT: obelisk.sv.
