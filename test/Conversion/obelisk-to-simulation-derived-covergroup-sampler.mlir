// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 19.4.1: a derived embedded covergroup replaces the base
// instance, so a group extending a live base group is live even when no code
// names it, and a sample through the base reads the derived class's
// properties: the owner is cast to the derived class.
//
//   class B;
//     covergroup cg; endgroup
//     function new(); cg = new; endfunction
//     function void take_sample(); cg.sample(); endfunction
//   endclass
//   class D extends B;
//     bit y;
//     covergroup extends cg;
//       coverpoint y;
//     endgroup
//   endclass
//   module top;
//     D obj;
//     initial begin
//       obj = new;
//       obj.take_sample();
//     end
//   endmodule

// CHECK: simulation.covergroup.decl @[[BASE:[^ ]*]] schema
// CHECK: simulation.covergroup.decl @[[DERIVED:[^ ]*]] schema {{[0-9]+}} base @[[BASE]]
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: %[[IS_D:.*]] = simulation.class.is_instance %arg1 is @[[D:[^ ]*]] :
// CHECK: cf.cond_br %[[IS_D]]
// CHECK: %[[OWNER:.*]] = simulation.class.cast %arg1 : {{.*}} to !simulation.class_handle<@[[D]]>
// CHECK: simulation.class.field_ref %[[OWNER]][@{{.*}}_D_field_0]
// CHECK: simulation.covergroup.sample %arg0, %{{.*}} values[%{{.*}}] ids [{{[0-9]+}}] : (!simulation.context, !simulation.covergroup_handle<@[[DERIVED]]>

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.B attributes {bitstream_width = 0 : i64, constructor_path = "B::new", constructor_symbol = @s1.$root::@s2::@s3.B::@s20.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "B", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "B", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>, this_variable_path = "B::this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s82.this} {
        obelisk.sv.type.covergroup_type @s4 attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "B", node_id = 4 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>} {
          obelisk.sv.symbol.covergroup_body @s5 attributes {hierarchical_name = "B", node_id = 5 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.class_property @s6.option attributes {hierarchical_name = "B.option", is_compiler_generated, name = "option", node_id = 6 : i64, semantic_type = !obelisk.source_aggregate<"B", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
            }
            obelisk.sv.symbol.class_property @s7.type_option attributes {hierarchical_name = "B.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 7 : i64, semantic_type = !obelisk.source_aggregate<"B", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.real}]>} {
            }
            obelisk.sv.symbol.subroutine @s18.sample attributes {hierarchical_name = "B.sample", is_builtin, name = "sample", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 28 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.class_property @s19.cg attributes {hierarchical_name = "B::cg", is_const, name = "cg", node_id = 29 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>} {
        }
        obelisk.sv.symbol.subroutine @s20.new attributes {hierarchical_name = "B::new", is_constructor, name = "new", node_id = 30 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "B::new.this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s20.new::@s21.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "B::cg", referenced_symbol = @s1.$root::@s2::@s3.B::@s19.cg, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>} {
              }
              obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, defaulted_arguments = array<i64>, is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s21.this attributes {hierarchical_name = "B::new.this", is_compiler_generated, is_const, name = "this", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>} {
          }
        }
        obelisk.sv.symbol.subroutine @s22.take_sample attributes {hierarchical_name = "B::take_sample", name = "take_sample", node_id = 36 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "B::take_sample.this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s22.take_sample::@s23.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 38 : i64, referenced_path = "B.sample", referenced_symbol = @s1.$root::@s2::@s3.B::@s4::@s5::@s18.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "B::cg", referenced_symbol = @s1.$root::@s2::@s3.B::@s19.cg, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s23.this attributes {hierarchical_name = "B::take_sample.this", is_compiler_generated, is_const, name = "this", node_id = 40 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>} {
          }
        }
        obelisk.sv.symbol.variable @s82.this attributes {hierarchical_name = "B::this", is_compiler_generated, is_const, name = "this", node_id = 143 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>} {
        }
      }
      obelisk.sv.type.class_type @s36.D attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.B>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 61 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>, this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s36.D::@s83.this} {
        obelisk.sv.symbol.transparent_member @s37.take_sample attributes {hierarchical_name = "D::take_sample", name = "take_sample", node_id = 62 : i64} {
        }
        obelisk.sv.symbol.class_property @s38.y attributes {hierarchical_name = "D::y", name = "y", node_id = 63 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.type.covergroup_type @s39 attributes {base_group = !obelisk.covergroup_handle<@s1.$root::@s2::@s3.B::@s4>, constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 4 : i32, has_coverage_event = false, hierarchical_name = "D", node_id = 64 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s36.D::@s39>} {
          obelisk.sv.symbol.covergroup_body @s40 attributes {hierarchical_name = "D", node_id = 65 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.class_property @s41.option attributes {hierarchical_name = "D.option", is_compiler_generated, name = "option", node_id = 66 : i64, semantic_type = !obelisk.source_aggregate<"D", false, false, false, false, false, false, 0, 166, 0, 0, [{name = "name", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "weight", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 2 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 3 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "at_least", ordinal = 4 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 5 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_num_print_missing", ordinal = 6 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "cross_retain_auto_bins", ordinal = 7 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "detect_overlap", ordinal = 8 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "per_instance", ordinal = 9 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "get_inst_coverage", ordinal = 10 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
            }
            obelisk.sv.symbol.class_property @s42.type_option attributes {hierarchical_name = "D.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 67 : i64, semantic_type = !obelisk.source_aggregate<"D", false, false, false, false, false, false, 0, 132, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "strobe", ordinal = 3 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "merge_instances", ordinal = 4 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "distribute_first", ordinal = 5 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}, {name = "real_interval", ordinal = 6 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.real}]>} {
            }
            obelisk.sv.symbol.transparent_member @s53.sample attributes {hierarchical_name = "D.sample", name = "sample", node_id = 87 : i64} {
            }
            obelisk.sv.symbol.coverpoint @s54.y attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "D.y", name = "y", node_id = 88 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 89 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 90 : i64, referenced_path = "D::y", referenced_symbol = @s1.$root::@s2::@s36.D::@s38.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
              obelisk.sv.symbol.class_property @s55.option attributes {hierarchical_name = "D.y.option", is_compiler_generated, name = "option", node_id = 91 : i64, semantic_type = !obelisk.source_aggregate<"D.y", false, false, false, false, false, false, 0, 130, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "at_least", ordinal = 3 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "auto_bin_max", ordinal = 4 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "detect_overlap", ordinal = 5 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              }
              obelisk.sv.symbol.class_property @s56.type_option attributes {hierarchical_name = "D.y.type_option", is_compiler_generated, lifetime = 1 : i32, name = "type_option", node_id = 92 : i64, semantic_type = !obelisk.source_aggregate<"D.y", false, false, false, false, false, false, 0, 129, 0, 0, [{name = "weight", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "goal", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "comment", ordinal = 2 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.string}, {name = "real_interval", ordinal = 3 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.real}]>} {
              }
            }
          }
        }
        obelisk.sv.symbol.class_property @s65.cg attributes {hierarchical_name = "D::cg", is_const, name = "cg", node_id = 109 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s2::@s36.D::@s39>} {
        }
        obelisk.sv.symbol.variable @s83.this attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 144 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s78.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 130 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s79.top attributes {hierarchical_name = "top", name = "top", node_id = 131 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s80.obj attributes {hierarchical_name = "top.obj", lifetime = 1 : i32, name = "obj", node_id = 132 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>} {
        }
        obelisk.sv.symbol.procedural_block @s81 attributes {hierarchical_name = "top", node_id = 133 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 134 : i64} {
            obelisk.sv.statement.list attributes {node_id = 135 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 136 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 137 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 138 : i64, referenced_path = "top.obj", referenced_symbol = @s1.$root::@s78.top::@s79.top::@s80.obj, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 139 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 140 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "take_sample", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 141 : i64, referenced_path = "B::take_sample", referenced_symbol = @s1.$root::@s2::@s3.B::@s22.take_sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 142 : i64, referenced_path = "top.obj", referenced_symbol = @s1.$root::@s78.top::@s79.top::@s80.obj, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s36.D>} {
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

