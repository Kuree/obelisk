// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --implicit-check-not="schedule = convergence"

// Two port connections in a chain: the inner instance's `assign` drives
// `middle_inst.inner_inst.out`, one propagation carries that to
// `middle_inst.out`, and a second carries it to `seen`, which an initial
// procedure reads at time zero.
//
// The two propagations are ordered by what they carry. IEEE 1800-2017 10.3
// makes a continuous assignment (and 23.3.3 a port connection) re-evaluate
// whenever its source changes, and the graph already records that as the
// producer waking the consumer. Ordering the consumer first would contradict
// its own sensitivity edge, and the pair would become a cycle that only a
// fixpoint could settle -- leaving the time-zero read of `seen` looking at a
// value nothing had produced yet.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inner", name = "inner", node_id = 0 : i64, sym_name = "s0.inner"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "middle", name = "middle", node_id = 1 : i64, sym_name = "s1.middle"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "settle_chain", name = "settle_chain", node_id = 2 : i64, sym_name = "s2.settle_chain"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64, sym_name = "s3.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 4 : i64, sym_name = "s4"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "settle_chain", is_uninstantiated = false, name = "settle_chain", node_id = 5 : i64, referenced_path = "settle_chain", referenced_symbol = @s2.settle_chain, sym_name = "s5.settle_chain"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "settle_chain", name = "settle_chain", node_id = 6 : i64, sym_name = "s6.settle_chain", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "settle_chain.seen", lifetime = 1 : i32, name = "seen", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.seen"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "settle_chain.middle_inst", is_uninstantiated = false, name = "middle_inst", node_id = 8 : i64, referenced_path = "middle", referenced_symbol = @s1.middle, sym_name = "s8.middle_inst"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "out", formal_ordinal = 0 : i64, formal_path = "settle_chain.middle_inst.out", formal_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s8.middle_inst::@s9.middle::@s10.out, formal_type = !obelisk.integral<32, true, false, 31 : 0, int>, internal_path = "settle_chain.middle_inst.out", internal_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s8.middle_inst::@s9.middle::@s11.out, is_ansi = true, is_net = false, node_id = 9 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "settle_chain.seen", referenced_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s7.seen, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "settle_chain.middle_inst", name = "middle", node_id = 13 : i64, sym_name = "s9.middle", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "settle_chain.middle_inst.out", name = "out", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s10.out"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "settle_chain.middle_inst.out", lifetime = 1 : i32, name = "out", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.out"} {
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "settle_chain.middle_inst.inner_inst", is_uninstantiated = false, name = "inner_inst", node_id = 16 : i64, referenced_path = "inner", referenced_symbol = @s0.inner, sym_name = "s12.inner_inst"} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "out", formal_ordinal = 0 : i64, formal_path = "settle_chain.middle_inst.inner_inst.out", formal_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s8.middle_inst::@s9.middle::@s12.inner_inst::@s13.inner::@s14.out, formal_type = !obelisk.integral<32, true, false, 31 : 0, int>, internal_path = "settle_chain.middle_inst.inner_inst.out", internal_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s8.middle_inst::@s9.middle::@s12.inner_inst::@s13.inner::@s15.out, is_ansi = true, is_net = false, node_id = 17 : i64, provenance = 1 : i32} {
              } {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, referenced_path = "settle_chain.middle_inst.out", referenced_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s8.middle_inst::@s9.middle::@s11.out, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "settle_chain.middle_inst.inner_inst", name = "inner", node_id = 21 : i64, sym_name = "s13.inner", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "settle_chain.middle_inst.inner_inst.out", name = "out", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.out"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "settle_chain.middle_inst.inner_inst.out", lifetime = 1 : i32, name = "out", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.out"} {
                }
                obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "settle_chain.middle_inst.inner_inst", node_id = 24 : i64, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 26 : i64, referenced_path = "settle_chain.middle_inst.inner_inst.out", referenced_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s8.middle_inst::@s9.middle::@s12.inner_inst::@s13.inner::@s15.out, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "13", is_declared_unsized = true, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "settle_chain", node_id = 28 : i64, procedure_kind = 0 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.settle_chain", system_scope_path = "settle_chain", system_scope_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain} {
              obelisk.sv.expression.string_literal attributes {constant_value = "seen=%0d", is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 32 : i64, referenced_path = "settle_chain.seen", referenced_symbol = @s3.$root::@s5.settle_chain::@s6.settle_chain::@s7.seen, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: compute_graph = #obelisk_sim.graph<
// The producer of `middle_inst.out` wakes its consumer ...
// CHECK-SAME: #obelisk_sim.edge<source = 6, target = 4, kind = sensitivity
// ... so the order chosen for the two conflicting writers runs it first.
// CHECK-SAME: #obelisk_sim.edge<source = 6, target = 4, kind = conflict
// CHECK-SAME: groups = [
// CHECK-SAME: #obelisk_sim.group<fragments = [6], schedule = acyclic
// CHECK-SAME: #obelisk_sim.group<fragments = [4], schedule = acyclic

// The root initializer spawns the inner propagation (@unit_3, which drives
// `middle_inst.out`) before the outer one (@unit_2, which reads it), so one
// time-zero pass carries the value the whole way to `seen`. @unit_0 is the
// `assign` the chain starts from and @unit_1 the initial procedure that reads
// the end of it.
// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK:      obelisk_sim.spawn @unit_0
// CHECK:      obelisk_sim.spawn @unit_3
// CHECK:      obelisk_sim.spawn @unit_2
// CHECK:      obelisk_sim.spawn @unit_1
