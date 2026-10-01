// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/clocking_access_direction = 1 : i32/clocking_access_direction = 2 : i32/g' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INOUT
// RUN: sed 's/clocking_access_direction = 1 : i32/clocking_access_direction = 0 : i32/g' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=INPUT-WRITE
// RUN: sed 's/clocking_output_skew_delay = "0"/clocking_output_skew_one_step/g' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=ONE-STEP

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s6.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s7.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s8.s attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s14.clk2 attributes {hierarchical_name = "top.clk2", lifetime = 1 : i32, name = "clk2", node_id = 49 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s15.t attributes {hierarchical_name = "top.t", lifetime = 1 : i32, name = "t", node_id = 50 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s18.marker attributes {hierarchical_name = "top.marker", lifetime = 1 : i32, name = "marker", node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.clocking_block @s9.cb attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 9 : i64} {
          obelisk.sv.symbol.clock_var @s10.q attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 12 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          obelisk.sv.symbol.clock_var @s11.r attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.r", input_edge = 0 : i32, lifetime = 1 : i32, name = "r", node_id = 14 : i64, output_edge = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          obelisk.sv.symbol.clock_var @s12.s attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = true, hierarchical_name = "top.cb.s", input_edge = 0 : i32, lifetime = 1 : i32, name = "s", node_id = 16 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        }
        obelisk.sv.symbol.clocking_block @s16.cb2 attributes {hierarchical_name = "top.cb2", is_default = false, is_global = false, name = "cb2", node_id = 51 : i64} {
          obelisk.sv.symbol.clock_var @s17.t attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb2.t", input_edge = 0 : i32, lifetime = 1 : i32, name = "t", node_id = 52 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        }
        obelisk.sv.symbol.procedural_block @s13 attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 21 : i64} {
            obelisk.sv.statement.list attributes {node_id = 22 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 25 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 29 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 30 : i64} {
                  obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 31 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb, semantic_type = !obelisk.void} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 32 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 60 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 61 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 62 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 63 : i64} {}
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 34 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 35 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 36 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_edge = 2 : i32, clocking_output_skew_edge_only, clocking_source_path = "top.r", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.r, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 41 : i64, referenced_path = "top.cb.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s11.r, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 42 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 46 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "2", clocking_output_skew_delay_is_real = false, clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.s", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s8.s, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 47 : i64, referenced_path = "top.cb.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s12.s, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 48 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 53 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 54 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk2", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s14.clk2, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.t", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s15.t, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 55 : i64, referenced_path = "top.cb2.t", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.cb2::@s17.t, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 71 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, node_id = 72 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 73 : i64} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_signed = true, node_id = 74 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                  }
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 75 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 76 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 77 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 78 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "top.marker", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s18.marker, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 80 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// Static zero-skew clocks are registered once per event, even when several
// outputs use the same clock.
// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: %[[CLK:.+]] = simulation.context.storage %arg0[0]
// CHECK: %[[CLK2:.+]] = simulation.context.storage %arg0[4]
// CHECK: simulation.clocking_output.track posedge %[[CLK]] width 1
// CHECK-NEXT: simulation.clocking_output.track posedge %[[CLK2]] width 1
// CHECK-NOT: simulation.clocking_output.track
// CHECK: simulation.return

// An asynchronous drive uses the current clocking time slot when available,
// otherwise waits for the next clocking edge before scheduling NBA.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.25
// CHECK-SAME: home_region = 10 : i32
// CHECK: %[[CURRENT:.+]] = simulation.clocking_output.current posedge %arg2 width 1
// CHECK-NEXT: cf.cond_br %[[CURRENT]], ^[[DRIVE:bb[0-9]+]], ^[[WAIT:bb[0-9]+]]
// CHECK: ^[[WAIT]]:
// CHECK: simulation.suspend.edge posedge
// CHECK: ^[[DRIVE]]:
// CHECK: simulation.nba.enqueue {{.*}} {clocking_output = [[Q_GROUP:[0-9]+]] : i64

// A same-edge drive following @(cb); ##0 remains in the current event.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.35
// CHECK-SAME: home_region = 10 : i32
// CHECK-NOT: simulation.clocking_output.current
// CHECK-NOT: simulation.suspend.edge
// CHECK: simulation.nba.enqueue {{.*}} {clocking_output = [[Q_GROUP]] : i64

// A distinct output edge remains a future synchronization point after @(cb).
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.41
// CHECK: simulation.suspend.edge negedge
// CHECK: simulation.nba.enqueue

// Positive output skew is expressed as a delayed NBA in precision ticks.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.47
// CHECK: simulation.time.constant 2{{$|[^0-9]}}
// CHECK: simulation.nba.enqueue {{.*}} after

// The current occurrence of cb does not synchronize a same-edge cb2 drive.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.55
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.nba.enqueue

// An intra-assignment cycle delay runs in the drive process. It captures the
// RHS, counts events there, and does not suspend the issuing process.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.75
// CHECK: {{^ *}}^[[WAIT:bb[0-9]+]](%[[COUNT:[a-zA-Z0-9_]+]]: i32)
// CHECK: simulation.suspend.edge posedge {{.*}} to ^[[RESUME:bb[0-9]+]](%[[COUNT]] : i32)
// CHECK: {{^ *}}^[[RESUME]](%[[RESUMED:[a-zA-Z0-9_]+]]: i32)
// CHECK: %[[REMAINING:.*]] = arith.subi %[[RESUMED]]
// CHECK: cf.cond_br {{.*}}, ^[[WAIT]](%[[REMAINING]] : i32), ^[[DRIVE:bb[0-9]+]]
// CHECK: {{^ *}}^[[DRIVE]]
// CHECK: simulation.nba.enqueue

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.spawn @unit_0.$clocking_output.25
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 10 : i32
// CHECK: simulation.spawn @unit_0.$clocking_output.35
// CHECK: simulation.spawn @unit_0.$clocking_output.41
// CHECK: simulation.spawn @unit_0.$clocking_output.47
// CHECK: simulation.spawn @unit_0.$clocking_output.55
// CHECK: simulation.spawn @unit_0.$clocking_output.75
// CHECK-NOT: simulation.suspend
// CHECK: simulation.ref.store
// CHECK-NOT: obelisk.sv.

// INOUT: simulation.func private @unit_0.$clocking_output.25
// INPUT-WRITE: cannot write an input clocking variable
// ONE-STEP: #1step is not a valid clocking output skew
