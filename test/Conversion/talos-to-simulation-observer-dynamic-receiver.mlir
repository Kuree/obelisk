// RUN: %split-file %s %t
// RUN: talos-opt %t/interface.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=INTERFACE
// RUN: talos-opt %t/class.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=CLASS

// IEEE 1800-2023 9.4.2, 9.4.3, 25.9: an event or wait expression is
// re-evaluated when any operand changes, including the virtual interface or
// class handle that selects the storage it reads. Such an expression is
// evaluated by an observer rather than a direct wait on the storage selected
// at suspension. The observer of a virtual-interface member also watches
// every storage the member can select, since no captured path names them.
// A member with a single elaborated instance keeps its direct wait. A method
// naming an instance property reads it through `this` (8.11).
//
// interface.sv:
//   interface Bus;
//     bit ready;
//   endinterface
//   interface One;
//     bit go;
//   endinterface
//   module top;
//     Bus a(), b();
//     One o();
//     virtual Bus v = a;
//     virtual One w = o;
//     initial wait (v.ready);
//     initial @(posedge v.ready);
//     initial wait (w.go);
//   endmodule
//
// class.sv:
//   class C;
//     bit [3:0] arr[2];
//     bit ready;
//     task run();
//       wait (ready);
//     endtask
//   endclass
//   module top;
//     C h = new;
//     initial wait (h.arr[1][2]);
//     initial h.run();
//   endmodule

// INTERFACE-LABEL: simulation.func private @unit_2(
// INTERFACE-DAG: %[[B:.*]] = simulation.context.storage %arg0[1] : !simulation.ref<i1>
// INTERFACE-DAG: %[[A:.*]] = simulation.context.storage %arg0[0] : !simulation.ref<i1>
// INTERFACE: %[[WAIT:.*]] = simulation.observer.bind @observer_{{[0-9_]+}} values(%arg1, %arg1, %[[A]], %[[B]] :
// INTERFACE-NOT: simulation.suspend.level
// INTERFACE: simulation.suspend.observe %[[WAIT]]
// INTERFACE-LABEL: simulation.func private @unit_3(
// INTERFACE-DAG: %[[B:.*]] = simulation.context.storage %arg0[1] : !simulation.ref<i1>
// INTERFACE-DAG: %[[A:.*]] = simulation.context.storage %arg0[0] : !simulation.ref<i1>
// INTERFACE: %[[EVENT:.*]] = simulation.observer.bind @observer_{{[0-9_]+}} values(%arg1, %arg1, %[[A]], %[[B]] :
// INTERFACE-NOT: simulation.suspend.edge
// INTERFACE: simulation.suspend.observe %[[EVENT]], %{{.*}}, %arg1 conditions 0 edges [1]
// INTERFACE-LABEL: simulation.func private @unit_4(
// INTERFACE-NOT: simulation.observer.bind
// INTERFACE: simulation.suspend.level

// CLASS-LABEL: simulation.func private @unit_0(
// CLASS: %[[READY:.*]] = simulation.managed.watch field
// CLASS: %[[TASK:.*]] = simulation.observer.bind @observer_{{[0-9_]+}} values(%arg1, %[[READY]] :
// CLASS: simulation.suspend.observe %[[TASK]]
// CLASS-LABEL: simulation.func private @unit_2(
// CLASS: %[[WATCH:.*]] = simulation.managed.watch field
// CLASS: %[[OBSERVER:.*]] = simulation.observer.bind @observer_{{[0-9_]+}} values(%arg1, %arg1, %[[WATCH]] :
// CLASS: simulation.suspend.observe %[[OBSERVER]]

//--- interface.mlir
module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.Bus attributes {definition_kind = 1 : i32, hierarchical_name = "Bus", name = "Bus", node_id = 0 : i64} {
  }
  talos.sv.symbol.definition @s1.One attributes {definition_kind = 1 : i32, hierarchical_name = "One", name = "One", node_id = 1 : i64} {
  }
  talos.sv.symbol.definition @s2.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 2 : i64} {
  }
  talos.sv.symbol.root @s3.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64} {
    talos.sv.symbol.compilation_unit @s4 attributes {hierarchical_name = "$unit", node_id = 4 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s5.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 5 : i64, referenced_path = "top", referenced_symbol = @s2.top} {
      talos.sv.symbol.instance_body @s6.top attributes {hierarchical_name = "top", name = "top", node_id = 6 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.instance @s7.a attributes {hierarchical_name = "top.a", is_uninstantiated = false, name = "a", node_id = 7 : i64, referenced_path = "Bus", referenced_symbol = @s0.Bus} {
          talos.sv.symbol.instance_body @s8.Bus attributes {hierarchical_name = "top.a", name = "Bus", node_id = 8 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "Bus", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.a, vpi_scope_kind = 601 : i32} {
            talos.sv.symbol.variable @s9.ready attributes {hierarchical_name = "top.a.ready", lifetime = 1 : i32, name = "ready", node_id = 9 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        talos.sv.symbol.instance @s10.b attributes {hierarchical_name = "top.b", is_uninstantiated = false, name = "b", node_id = 10 : i64, referenced_path = "Bus", referenced_symbol = @s0.Bus} {
          talos.sv.symbol.instance_body @s11.Bus attributes {hierarchical_name = "top.b", name = "Bus", node_id = 11 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "Bus", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.a, vpi_scope_kind = 601 : i32} {
            talos.sv.symbol.variable @s12.ready attributes {hierarchical_name = "top.b.ready", lifetime = 1 : i32, name = "ready", node_id = 12 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        talos.sv.symbol.instance @s13.o attributes {hierarchical_name = "top.o", is_uninstantiated = false, name = "o", node_id = 13 : i64, referenced_path = "One", referenced_symbol = @s1.One} {
          talos.sv.symbol.instance_body @s14.One attributes {hierarchical_name = "top.o", name = "One", node_id = 14 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "One", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s13.o, vpi_scope_kind = 601 : i32} {
            talos.sv.symbol.variable @s15.go attributes {hierarchical_name = "top.o.go", lifetime = 1 : i32, name = "go", node_id = 15 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        talos.sv.symbol.variable @s16.v attributes {hierarchical_name = "top.v", lifetime = 1 : i32, name = "v", node_id = 16 : i64, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.a, "">} {
          talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.a, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.a, "">} {
          }
        }
        talos.sv.symbol.variable @s17.w attributes {hierarchical_name = "top.w", lifetime = 1 : i32, name = "w", node_id = 18 : i64, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s13.o, "">} {
          talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.o", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s13.o, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s13.o, "">} {
          }
        }
        talos.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.wait attributes {node_id = 21 : i64} {
            talos.sv.expression.member_access attributes {is_signed = false, member_name = "ready", node_id = 22 : i64, referenced_path = "top.Bus.ready", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s21.Bus::@s22.ready, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.v", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s16.v, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.a, "">} {
              }
            }
            talos.sv.statement.empty attributes {node_id = 24 : i64} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "top", node_id = 25 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.timed attributes {node_id = 26 : i64} {
            talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 27 : i64} {
              talos.sv.expression.member_access attributes {is_signed = false, member_name = "ready", node_id = 28 : i64, referenced_path = "top.Bus.ready", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s21.Bus::@s22.ready, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.v", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s16.v, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.a, "">} {
                }
              }
            }
            talos.sv.statement.empty attributes {node_id = 30 : i64} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s20 attributes {hierarchical_name = "top", node_id = 31 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.wait attributes {node_id = 32 : i64} {
            talos.sv.expression.member_access attributes {is_signed = false, member_name = "go", node_id = 33 : i64, referenced_path = "top.One.go", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s23.One::@s24.go, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "top.w", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s17.w, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s13.o, "">} {
              }
            }
            talos.sv.statement.empty attributes {node_id = 35 : i64} {
            }
          }
        }
        talos.sv.symbol.instance_body @s21.Bus attributes {hierarchical_name = "top.Bus", is_virtual_interface_type_instance = true, name = "Bus", node_id = 36 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "Bus", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.a, vpi_scope_kind = 601 : i32} {
          talos.sv.symbol.variable @s22.ready attributes {hierarchical_name = "top.Bus.ready", lifetime = 1 : i32, name = "ready", node_id = 37 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.instance_body @s23.One attributes {hierarchical_name = "top.One", is_virtual_interface_type_instance = true, name = "One", node_id = 38 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "One", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s13.o, vpi_scope_kind = 601 : i32} {
          talos.sv.symbol.variable @s24.go attributes {hierarchical_name = "top.One.go", lifetime = 1 : i32, name = "go", node_id = 39 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
      }
    }
  }
}


//--- class.mlir
module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.C attributes {bitstream_width = 9 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s25.this} {
        talos.sv.symbol.class_property @s4.arr attributes {hierarchical_name = "C::arr", name = "arr", node_id = 4 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>} {
        }
        talos.sv.symbol.class_property @s5.ready attributes {hierarchical_name = "C::ready", name = "ready", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.subroutine @s6.run attributes {hierarchical_name = "C::run", name = "run", node_id = 6 : i64, semantic_type = !talos.subroutine<() -> (), true>, subroutine_kind = 1 : i32, this_variable_path = "C::run.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.run::@s7.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.wait attributes {node_id = 7 : i64} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 8 : i64, referenced_path = "C::ready", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.ready, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
            talos.sv.statement.empty attributes {node_id = 9 : i64} {
            }
          }
          talos.sv.symbol.variable @s7.this attributes {hierarchical_name = "C::run.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        talos.sv.symbol.subroutine @s8.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 11 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        talos.sv.symbol.variable @s25.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 48 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    talos.sv.symbol.instance @s20.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 31 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s21.top attributes {hierarchical_name = "top", name = "top", node_id = 32 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s22.h attributes {hierarchical_name = "top.h", lifetime = 1 : i32, name = "h", node_id = 33 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 34 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        talos.sv.symbol.procedural_block @s23 attributes {hierarchical_name = "top", node_id = 35 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.wait attributes {node_id = 36 : i64} {
            talos.sv.expression.element_select attributes {is_signed = false, node_id = 37 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 38 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                talos.sv.expression.member_access attributes {is_signed = false, member_name = "arr", node_id = 39 : i64, referenced_path = "C::arr", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.arr, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.h", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22.h, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                }
                talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 41 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
              talos.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 42 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
            }
            talos.sv.statement.empty attributes {node_id = 43 : i64} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s24 attributes {hierarchical_name = "top", node_id = 44 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 45 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "run", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 46 : i64, referenced_path = "C::run", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.run, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "top.h", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22.h, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

