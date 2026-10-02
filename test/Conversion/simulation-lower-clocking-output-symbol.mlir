// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s < %t.threaded

// Already-prepared inputs isolate function lowering from semantic preparation.
// The clocking output expression has frozen captures for top.a and top.b.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-COUNT-2: simulation.ref.subelement
// CHECK: simulation.spawn @unit_0.$clocking_output.22.1
// CHECK: simulation.spawn @unit_0.$clocking_output.22.2

#loc = loc("clocking.sv":1:1)
#loc1 = loc("clocking.sv":2:1)
module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_anchor = @__obelisk_vpi_anchor_0} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_anchor = @__obelisk_vpi_anchor_1, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s6.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s7.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        // This unused declaration has the same leaf name as cb.out. Resolve
        // the full symbol reference without traversing sibling function bodies.
        obelisk.sv.symbol.clocking_block @decoy attributes {hierarchical_name = "top.decoy", is_default = false, is_global = false, name = "decoy", node_id = 26 : i64} {
          obelisk.sv.symbol.clock_var @s9.out attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.decoy.out", input_edge = 0 : i32, lifetime = 1 : i32, name = "out", node_id = 27 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
        }
        obelisk.sv.symbol.clocking_block @s8.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 8 : i64, simulation.vpi_anchor = @__obelisk_vpi_anchor_2} {
          obelisk.sv.symbol.clock_var @s9.out attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = true, hierarchical_name = "top.cb.out", input_edge = 0 : i32, lifetime = 1 : i32, name = "out", node_id = 11 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.concatenation attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.element_select attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.element_select attributes {node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.timing.delay attributes {node_id = 24 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 19 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 22 : i64, referenced_path = "top.cb.out", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.out, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "2'b10", node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
      }
    }
  }
  simulation.design @design attributes {time_precision_fs = 1000000 : i64} {
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.vpi_definition.decl @__obelisk_vpi_definition_0 type 32 name "top" definition_loc #loc
    simulation.scope.decl 1 parent 0 hierarchy "top" debug "top" source_definition "top" vpi_kind 32 definition @__obelisk_vpi_definition_0 {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_0 id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "" {definition_loc = #loc1, is_compilation_unit, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 9 : i32, value = "$unit">, #simulation.vpi_property<selector = 600 : i32, value = true>, #simulation.vpi_property<selector = 602 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_1 id 1 type 32 in 1 ordinal 1 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 7 : i32, value = true>, #simulation.vpi_property<selector = 9 : i32, value = "top">, #simulation.vpi_property<selector = 600 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_2 id 2 type 650 in 1 parent @__obelisk_vpi_anchor_1 ordinal 0 hierarchy "top.cb" debug "cb"
    simulation.storage.decl 0 in 1 : !simulation.logic<1> design hierarchy "top.clk" debug "clk" {obelisk.coverage.source_authored, vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>}
    simulation.storage.decl 1 in 1 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design hierarchy "top.a" debug "a" {obelisk.coverage.source_authored, vpi_type = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = true, range = [1, 0], children = [#simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>], childNames = []>}
    simulation.storage.decl 2 in 1 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design hierarchy "top.b" debug "b" {obelisk.coverage.source_authored, vpi_type = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = true, range = [1, 0], children = [#simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>], childNames = []>}
    simulation.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer" {internal}
    simulation.code_unit.decl 8546119225122879361 in 1 initial hierarchy "top.$code_unit_19" debug ""
    simulation.func @__obelisk_root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, entry_kind = 0 : i32, home_region = 2 : i32} {
      %0 = simulation.context.storage %arg0[0] : !simulation.ref<!simulation.logic<1>>
      %1 = simulation.context.storage %arg0[1] : !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      %2 = simulation.context.storage %arg0[2] : !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      %3 = simulation.spawn @unit_0(%arg0, %0, %1, %2) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>, !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.process
      simulation.return
    }
    simulation.func private @unit_0(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %arg3: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {code_unit_id = 8546119225122879361 : i64, domain = 0 : i32, entry_kind = 1 : i32, home_region = 2 : i32, simulation.bindings = [#simulation.argument_binding<path = "top.clk", argument = 1, kind = direct, copyOut = false>, #simulation.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>, #simulation.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>], simulation.delay_quantum = 1 : i64, simulation.delay_scale = 1 : i64, simulation.hierarchical_name = "top"} {
      obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
        obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 22 : i64, referenced_path = "top.cb.out", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.out, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
          obelisk.sv.expression.integer_literal attributes {constant_value = "2'b10", node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
        }
      }
      simulation.return
    }
  }
}
