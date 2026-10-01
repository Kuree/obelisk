// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A static variable initializer can fold a net declaration's sole literal
// driver. Otherwise it reads the net's default Z value while the constant
// driver is still waiting to run as a continuous process.

module {
  obelisk.sv.symbol.definition @s0.constant_net_initializer attributes {
      definition_kind = 0 : i32,
      hierarchical_name = "constant_net_initializer",
      name = "constant_net_initializer", node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
        hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.constant_net_initializer attributes {
        hierarchical_name = "constant_net_initializer",
        is_uninstantiated = false, name = "constant_net_initializer",
        node_id = 3 : i64,
        referenced_path = "constant_net_initializer",
        referenced_symbol = @s0.constant_net_initializer
    } {
      obelisk.sv.symbol.instance_body @s4.constant_net_initializer attributes {
          hierarchical_name = "constant_net_initializer",
          name = "constant_net_initializer", node_id = 4 : i64
      } {
        obelisk.sv.symbol.net @s5.constant_net attributes {
            hierarchical_name = "constant_net_initializer.constant_net",
            is_implicit = false, name = "constant_net", net_kind = 1 : i32,
            node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "1'b1", node_id = 6 : i64,
              semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
        }
        obelisk.sv.symbol.variable @s6.observed attributes {
            hierarchical_name = "constant_net_initializer.observed",
            lifetime = 1 : i32, name = "observed", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {
          obelisk.sv.expression.named_value attributes {
              node_id = 8 : i64,
              referenced_path = "constant_net_initializer.constant_net",
              referenced_symbol = @s1.$root::@s3.constant_net_initializer::@s4.constant_net_initializer::@s5.constant_net,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        obelisk.sv.symbol.variable @s7.source attributes {
            hierarchical_name = "constant_net_initializer.source",
            lifetime = 1 : i32, name = "source", node_id = 9 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {
        }
        obelisk.sv.symbol.net @s8.dynamic_net attributes {
            hierarchical_name = "constant_net_initializer.dynamic_net",
            is_implicit = false, name = "dynamic_net", net_kind = 1 : i32,
            node_id = 10 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {
          obelisk.sv.expression.named_value attributes {
              node_id = 11 : i64,
              referenced_path = "constant_net_initializer.source",
              referenced_symbol = @s1.$root::@s3.constant_net_initializer::@s4.constant_net_initializer::@s7.source,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        obelisk.sv.symbol.net @s9.multiple_net attributes {
            hierarchical_name = "constant_net_initializer.multiple_net",
            is_implicit = false, name = "multiple_net", net_kind = 1 : i32,
            node_id = 12 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "1'b1", node_id = 13 : i64,
              semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
        }
        obelisk.sv.symbol.variable @s10.multiple_observed attributes {
            hierarchical_name = "constant_net_initializer.multiple_observed",
            lifetime = 1 : i32, name = "multiple_observed",
            node_id = 14 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {
          obelisk.sv.expression.named_value attributes {
              node_id = 15 : i64,
              referenced_path = "constant_net_initializer.multiple_net",
              referenced_symbol = @s1.$root::@s3.constant_net_initializer::@s4.constant_net_initializer::@s9.multiple_net,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        obelisk.sv.symbol.continuous_assign @s11 attributes {
            hierarchical_name = "constant_net_initializer", node_id = 16 : i64
        } {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 17 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {
                node_id = 18 : i64,
                referenced_path = "constant_net_initializer.multiple_net",
                referenced_symbol = @s1.$root::@s3.constant_net_initializer::@s4.constant_net_initializer::@s9.multiple_net,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 19 : i64,
                referenced_path = "constant_net_initializer.source",
                referenced_symbol = @s1.$root::@s3.constant_net_initializer::@s4.constant_net_initializer::@s7.source,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
      }
    }
  }
}

// The literal net keeps its continuous process so procedural listeners still
// observe its time-zero transition.
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} continuous hierarchy "constant_net_initializer.constant_net.$net_initializer"
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} function hierarchy "constant_net_initializer.observed.$static_initializer"
// A signal-dependent declaration assignment remains a continuous process.
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} continuous hierarchy "constant_net_initializer.dynamic_net.$net_initializer"
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} continuous hierarchy "constant_net_initializer.multiple_net.$net_initializer"
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} function hierarchy "constant_net_initializer.multiple_observed.$static_initializer"

// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: %[[NET:.*]] = simulation.context.net
// CHECK-NEXT: simulation.call @unit_1
// CHECK: simulation.spawn @unit_0
// CHECK: %[[STORAGE:.*]] = simulation.context.storage
// CHECK: simulation.spawn @unit_2
// CHECK: simulation.return

// The static initializer folds its read of the single literal driver instead
// of sampling the net's default Z value before processes start.
// CHECK-LABEL: simulation.func private @unit_1
// CHECK-SAME: entry_kind = 8 : i32
// CHECK-NOT: simulation.net.read
// CHECK: simulation.logic.constant true, false
// CHECK: simulation.ref.store
// CHECK: simulation.return

// A literal declaration assignment is not folded when another driver targets
// the same net; the initializer must sample the resolved net value instead.
// CHECK-LABEL: simulation.func private @unit_4
// CHECK-SAME: entry_kind = 8 : i32
// CHECK: simulation.net.read
// CHECK: simulation.ref.store
// CHECK: simulation.return
