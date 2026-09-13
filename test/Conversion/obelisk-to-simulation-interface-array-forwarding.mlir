// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s
// RUN: sed 's/"host"/""/g' %s | obelisk-opt --obelisk-sim-prepare | FileCheck %s
// RUN: sed 's/array_range = array<i64: 0, 2>/array_range = array<i64: 0, 3>/g' %s | not obelisk-opt --obelisk-sim-prepare 2>&1 | FileCheck %s --check-prefix=INVALID
// Forwarding a received interface array introduces a detached symbol stub.
// It must not acquire a duplicate VPI array identity or fail extent checks.
// CHECK: obelisk_sim.design
// CHECK: obelisk_sim.vpi_object.anchor {{.*}} hierarchy "top.rif" debug "rif"
// CHECK-NOT: obelisk_sim.vpi_object.anchor {{.*}} hierarchy "rif"
// INVALID: instance-array dimension has 3 elements but its source range requires 4

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "adapter", name = "adapter", node_id = 0 : i64, sym_name = "s0.adapter"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inner", name = "inner", node_id = 1 : i64, sym_name = "s1.inner"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "simple_if", name = "simple_if", node_id = 2 : i64, sym_name = "s2.simple_if"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 3 : i64, sym_name = "s3.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 4 : i64, sym_name = "s4.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 5 : i64, obelisk_sim.vpi_definition_name = "$unit", sym_name = "s5"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 6 : i64, referenced_path = "top", referenced_symbol = @s3.top, sym_name = "s6.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 7 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "top", obelisk_sim.vpi_top = true, sym_name = "s7.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 0, 2>, hierarchical_name = "top.rif", name = "rif", node_id = 8 : i64, sym_name = "s8.rif"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.rif[0]", is_uninstantiated = false, node_id = 9 : i64, referenced_path = "simple_if", referenced_symbol = @s2.simple_if, sym_name = "s9"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.rif[0]", name = "simple_if", node_id = 10 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "simple_if", obelisk_sim.vpi_top = false, sym_name = "s10.simple_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s4.$root::@s7.top::@s8.rif::@s9, vpi_scope_kind = 601 : i32} {
              obelisk.sv.symbol.variable attributes {hierarchical_name = "top.rif[0].v", lifetime = 1 : i32, name = "v", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.v"} {
              }
              obelisk.sv.symbol.modport attributes {hierarchical_name = "top.rif[0].host", name = "host", node_id = 12 : i64, sym_name = "s12.host"} {
                obelisk.sv.symbol.modport_port attributes {direction = 0 : i32, hierarchical_name = "top.rif[0].host.v", name = "v", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.v"} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.rif[0].v", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif::@s9::@s10.simple_if::@s11.v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.rif[1]", is_uninstantiated = false, node_id = 15 : i64, referenced_path = "simple_if", referenced_symbol = @s2.simple_if, sym_name = "s14"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.rif[1]", name = "simple_if", node_id = 16 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "simple_if", obelisk_sim.vpi_top = false, sym_name = "s15.simple_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s4.$root::@s7.top::@s8.rif::@s9, vpi_scope_kind = 601 : i32} {
              obelisk.sv.symbol.variable attributes {hierarchical_name = "top.rif[1].v", lifetime = 1 : i32, name = "v", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.v"} {
              }
              obelisk.sv.symbol.modport attributes {hierarchical_name = "top.rif[1].host", name = "host", node_id = 18 : i64, sym_name = "s17.host"} {
                obelisk.sv.symbol.modport_port attributes {direction = 0 : i32, hierarchical_name = "top.rif[1].host.v", name = "v", node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s18.v"} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.rif[1].v", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif::@s14::@s15.simple_if::@s16.v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.rif[2]", is_uninstantiated = false, node_id = 21 : i64, referenced_path = "simple_if", referenced_symbol = @s2.simple_if, sym_name = "s19"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.rif[2]", name = "simple_if", node_id = 22 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "simple_if", obelisk_sim.vpi_top = false, sym_name = "s20.simple_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s4.$root::@s7.top::@s8.rif::@s9, vpi_scope_kind = 601 : i32} {
              obelisk.sv.symbol.variable attributes {hierarchical_name = "top.rif[2].v", lifetime = 1 : i32, name = "v", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s21.v"} {
              }
              obelisk.sv.symbol.modport attributes {hierarchical_name = "top.rif[2].host", name = "host", node_id = 24 : i64, sym_name = "s22.host"} {
                obelisk.sv.symbol.modport_port attributes {direction = 0 : i32, hierarchical_name = "top.rif[2].host.v", name = "v", node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s23.v"} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.rif[2].v", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif::@s19::@s20.simple_if::@s21.v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.u_ad", is_uninstantiated = false, name = "u_ad", node_id = 27 : i64, referenced_path = "adapter", referenced_symbol = @s0.adapter, sym_name = "s24.u_ad"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "rif", formal_ordinal = 0 : i64, formal_path = "top.u_ad.rif", formal_symbol = @s4.$root::@s6.top::@s7.top::@s24.u_ad::@s25.adapter::@s27.rif, formal_type = !obelisk.untyped, interface_instance_path = "top.rif", interface_instance_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif, interface_shape = array<i64: 0, 2>, is_ansi = false, is_net = false, node_id = 28 : i64, provenance = 1 : i32, selected_modport = "host"} {
          } {
            obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.rif", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif, semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.virtual_interface<@s4.$root::@s7.top::@s8.rif::@s9, "">>} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.u_ad", name = "adapter", node_id = 30 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "adapter", obelisk_sim.vpi_top = false, sym_name = "s25.adapter", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
            obelisk.sv.symbol.parameter attributes {constant_value = "3", hierarchical_name = "top.u_ad.N", name = "N", node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s26.N"} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.symbol.interface_port attributes {hierarchical_name = "top.u_ad.rif", name = "rif", node_id = 33 : i64, sym_name = "s27.rif"} {
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.u_ad.u_inner", is_uninstantiated = false, name = "u_inner", node_id = 34 : i64, referenced_path = "inner", referenced_symbol = @s1.inner, sym_name = "s28.u_inner"} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "rif", formal_ordinal = 0 : i64, formal_path = "top.u_ad.u_inner.rif", formal_symbol = @s4.$root::@s6.top::@s7.top::@s24.u_ad::@s25.adapter::@s28.u_inner::@s29.inner::@s31.rif, formal_type = !obelisk.untyped, interface_instance_path = "rif", interface_instance_symbol = @s35.rif, interface_shape = array<i64: 0, 2>, is_ansi = false, is_net = false, node_id = 35 : i64, provenance = 1 : i32, selected_modport = "host"} {
              } {
                obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 36 : i64, referenced_path = "rif", referenced_symbol = @s35.rif, semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.virtual_interface<@s4.$root::@s7.top::@s8.rif::@s9, "host">>} {
                }
              }
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.u_ad.u_inner", name = "inner", node_id = 37 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "inner", obelisk_sim.vpi_top = false, sym_name = "s29.inner", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
                obelisk.sv.symbol.parameter attributes {constant_value = "3", hierarchical_name = "top.u_ad.u_inner.N", name = "N", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s30.N"} {
                  obelisk.sv.expression.named_value attributes {folded_constant = "3", is_signed = true, node_id = 39 : i64, referenced_path = "top.u_ad.N", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s24.u_ad::@s25.adapter::@s26.N, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.symbol.interface_port attributes {hierarchical_name = "top.u_ad.u_inner.rif", name = "rif", node_id = 40 : i64, sym_name = "s31.rif"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "top.u_ad.u_inner.got", lifetime = 1 : i32, name = "got", node_id = 41 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s32.got"} {
                }
                obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "top.u_ad.u_inner", node_id = 42 : i64, sym_name = "s33", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 43 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.u_ad.u_inner.got", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s24.u_ad::@s25.adapter::@s28.u_inner::@s29.inner::@s32.got, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.hierarchical_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.rif[0].host.v", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif::@s9::@s10.simple_if::@s12.host::@s13.v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 46 : i64, procedure_kind = 0 : i32, sym_name = "s34", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 47 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 48 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s4.$root::@s6.top::@s7.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "%b", is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.hierarchical_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.rif[0].v", referenced_symbol = @s4.$root::@s6.top::@s7.top::@s8.rif::@s9::@s10.simple_if::@s11.v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 0, 2>, hierarchical_name = "rif", name = "rif", node_id = 54 : i64, sym_name = "s35.rif"} {
  }
}
