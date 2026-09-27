// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=STRUCT
// RUN: sed 's/clocking_event_edge = 1 : i32/clocking_event_edge = 2 : i32/g' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=NEGEDGE

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.q"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 7 : i64, semantic_type = !obelisk.integral<96, true, false, 95 : 0, logic>, sym_name = "s7.n"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 8 : i64, sym_name = "s8.cb"} {
          obelisk.sv.symbol.clock_var attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 9 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.q"} {}
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 13 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 14 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 16 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 17 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 18 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 20 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 21 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 22 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "96'd3", node_id = 23 : i64, semantic_type = !obelisk.integral<96, false, false, 95 : 0, logic>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 24 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 40 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 41 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 43 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 25 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 26 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 27 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.n, semantic_type = !obelisk.integral<96, true, false, 95 : 0, logic>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 28 : i64} {}
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 31 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 53 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_signed = true, node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              }
              obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 55 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 14.11: the leading zero count has no clocking event behind
// it, so it waits for one; one, constant-many, and dynamic-many each need only
// one static suspension site, all resuming in Reactive.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.31
// CHECK-SAME: %arg3: i1
// CHECK-SAME: home_region = 10 : i32
// CHECK: cf.cond_br %arg3
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.nba.enqueue

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-COUNT-4: simulation.suspend.edge posedge {{.*}}resume_region = 10 : i32

// STRUCT-LABEL: simulation.func private @unit_0(
// The constant three-cycle delay is a counter loop, not three unrolled waits.
// STRUCT-DAG: arith.constant 3 : i96
// STRUCT-DAG: arith.subi
// STRUCT-DAG: arith.cmpi ne
// The dynamic count is evaluated once; nonpositive values take the false path.
// STRUCT-DAG: simulation.ref.load
// STRUCT-DAG: arith.cmpi sgt
// STRUCT-DAG: cf.cond_br
// STRUCT-DAG: simulation.spawn @unit_0.$clocking_output.31({{.*}}) : {{.*}}, i1
// STRUCT-NOT: obelisk.sv.

// An intra-assignment ## retains its pre-delay RHS and does not add another
// output-edge wait after its final clock cycle. The cycle counter runs in the
// outlined drive process so the issuing process does not suspend.
// CHECK-LABEL: simulation.func private @unit_1.$clocking_output.55
// CHECK-SAME: home_region = 10 : i32
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 10 : i32
// CHECK: simulation.nba.enqueue
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-NOT: simulation.suspend.edge
// CHECK: simulation.spawn @unit_1.$clocking_output.55

// NEGEDGE-COUNT-5: simulation.suspend.edge negedge
