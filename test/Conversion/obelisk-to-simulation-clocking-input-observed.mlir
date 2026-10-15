// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --implicit-check-not='top.cb.other.$sampled'

// IEEE 1800-2023 14.13 and 14.15: an event control, wait condition, or
// implicit event expression that reads a clocking input observes its sampled
// value, which changes only when the clocking block publishes a sample in
// Observed. Such an input gets one private storage that its single sampler
// writes after each update; observers and implicit waits depend on that
// storage instead of the raw clock and source. An input that is only read
// after its clocking event (`other`) needs no such storage.
//
// module top;
//   bit clk, value, flag, other, copy, plain;
//   clocking cb @(posedge clk); input #0 value, flag, other; endclocking
//   initial @(cb.value);
//   always @* copy = cb.value;
//   initial wait (cb.flag);
//   initial @(cb) plain = cb.other;
// endmodule

// CHECK: simulation.storage.decl [[VALUE:[0-9]+]] in {{[0-9]+}} : i1 design hierarchy "top.cb.value.$sampled" debug "__obelisk_clocking_sample"
// CHECK: simulation.storage.decl [[FLAG:[0-9]+]] in {{[0-9]+}} : i1 design hierarchy "top.cb.flag.$sampled" debug "__obelisk_clocking_sample"

// @(cb.value) depends only on the published sample.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %[[EVENT_SAMPLE:arg[0-9]+]]: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = [[VALUE]] : i64}
// CHECK: simulation.assert.clocked_sample_read %{{.*}} id [[VALUE_SITE:[0-9]+]]
// CHECK: simulation.observer.bind @{{.*}} values({{%arg[0-9]+, %arg[0-9]+}}, %[[EVENT_SAMPLE]], %[[EVENT_SAMPLE]] : {{.*}}) captures 3

// @* waits on the same storage and reads the same sampler.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %[[IMPLICIT_SAMPLE:arg[0-9]+]]: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = [[VALUE]] : i64}
// CHECK: simulation.suspend.change %[[IMPLICIT_SAMPLE]] to
// CHECK: simulation.assert.clocked_sample_read %{{.*}} id [[VALUE_SITE]]

// A clocking input is not addressable storage, so wait observes it.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: %[[WAIT_SAMPLE:arg[0-9]+]]: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = [[FLAG]] : i64}
// CHECK: simulation.observer.bind @{{.*}} values({{%arg[0-9]+, %arg[0-9]+}}, %[[WAIT_SAMPLE]], %[[WAIT_SAMPLE]] : {{.*}}) captures 3
// CHECK: simulation.suspend.observe

// CHECK-LABEL: simulation.func private @unit_3.$clocking_input.
// CHECK: simulation.assert.clocked_sample_update
// CHECK-NEXT: cf.br

// The sampler publishes after the update, in Observed.
// CHECK: simulation.func private @observer_{{[0-9]+}}_{{[0-9]+}}.$clocking_input.[[VALUE_SITE]](
// CHECK-SAME: %[[PUBLISHED:arg[0-9]+]]: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = [[VALUE]] : i64}
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 8 : i32
// CHECK: %[[SAMPLE:.*]] = simulation.ref.load
// CHECK: simulation.assert.clocked_sample_update %{{.*}} from %[[SAMPLE]] {{.*}} id [[VALUE_SITE]]
// CHECK-NEXT: simulation.ref.store %[[SAMPLE]] to %[[PUBLISHED]]

// CHECK: simulation.func private @observer_{{[0-9]+}}_{{[0-9]+}}.$clocking_input.
// CHECK-SAME: simulation.descriptor_id = [[FLAG]] : i64
// CHECK: simulation.assert.clocked_sample_update
// CHECK-NEXT: simulation.ref.store

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s7.flag attributes {hierarchical_name = "top.flag", lifetime = 1 : i32, name = "flag", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s8.other attributes {hierarchical_name = "top.other", lifetime = 1 : i32, name = "other", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s9.copy attributes {hierarchical_name = "top.copy", lifetime = 1 : i32, name = "copy", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s10.plain attributes {hierarchical_name = "top.plain", lifetime = 1 : i32, name = "plain", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.clocking_block @s11.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 11 : i64} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.clock_var @s12.value attributes {direction = 0 : i32, has_input_delay = true, has_output_delay = false, hierarchical_name = "top.cb.value", input_edge = 0 : i32, lifetime = 1 : i32, name = "value", node_id = 14 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
            obelisk.sv.timing.delay attributes {node_id = 16 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.clock_var @s13.flag attributes {direction = 0 : i32, has_input_delay = true, has_output_delay = false, hierarchical_name = "top.cb.flag", input_edge = 0 : i32, lifetime = 1 : i32, name = "flag", node_id = 18 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.flag", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.flag, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
            obelisk.sv.timing.delay attributes {node_id = 20 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.clock_var @s14.other attributes {direction = 0 : i32, has_input_delay = true, has_output_delay = false, hierarchical_name = "top.cb.other", input_edge = 0 : i32, lifetime = 1 : i32, name = "other", node_id = 22 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.other", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.other, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
            obelisk.sv.timing.delay attributes {node_id = 24 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "top", node_id = 26 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 27 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 28 : i64} {
              obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_delay = "0", clocking_input_skew_delay_is_real = false, clocking_input_skew_edge = 0 : i32, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.value", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 29 : i64, referenced_path = "top.cb.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.cb::@s12.value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 30 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s16 attributes {hierarchical_name = "top", node_id = 31 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 32 : i64} {
            obelisk.sv.timing.implicit_event attributes {node_id = 33 : i64} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.copy", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.copy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_delay = "0", clocking_input_skew_delay_is_real = false, clocking_input_skew_edge = 0 : i32, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.value", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 37 : i64, referenced_path = "top.cb.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.cb::@s12.value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s17 attributes {hierarchical_name = "top", node_id = 38 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait attributes {node_id = 39 : i64} {
            obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_delay = "0", clocking_input_skew_delay_is_real = false, clocking_input_skew_edge = 0 : i32, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.flag", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.flag, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 40 : i64, referenced_path = "top.cb.flag", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.cb::@s13.flag, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
            obelisk.sv.statement.empty attributes {node_id = 41 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 42 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 43 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 44 : i64} {
              obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, is_signed = false, node_id = 45 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.cb, semantic_type = !obelisk.void} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 46 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 47 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.plain", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.plain, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_delay = "0", clocking_input_skew_delay_is_real = false, clocking_input_skew_edge = 0 : i32, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.other", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s8.other, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 49 : i64, referenced_path = "top.cb.other", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.cb::@s14.other, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

