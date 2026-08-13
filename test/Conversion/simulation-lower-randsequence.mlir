// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 18.17.1 requires every weight of the selected production to
// be evaluated once before a rule is chosen. A zero total selects no rule;
// otherwise selection is proportional to the cumulative weights. Production
// arguments are activation-local automatic variables (18.17.7).

!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  obelisk_sim.design @randsequence {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "test.randsequence"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "test.controls"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "test.single_weight"
    obelisk_sim.scope.decl 0

    // CHECK-LABEL: obelisk_sim.func @weighted
    obelisk_sim.func @weighted(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [],
          code_unit_id = 1 : i64
        } {
      obelisk.sv.statement.rand_sequence attributes {
          first_production = @main, first_production_path = "main",
          has_first_production = true, node_id = 1 : i64,
          production_count = 2 : i64} {
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "main", referenced_symbol = @main,
            rule_count = 2 : i64,
            rule_has_rand_join_expressions = array<i64: 0, 0>,
            rule_has_weight_code_blocks = array<i64: 0, 0>,
            rule_has_weights = array<i64: 1, 1>,
            rule_is_rand_join = array<i64: 0, 0>,
            rule_item_counts = array<i64: 1, 1>,
            rule_variables = [[], []], semantic_type = !obelisk.void} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "2", is_signed = true, node_id = 3 : i64,
              semantic_type = !int} {
          }
          obelisk.sv.rand_seq.item attributes {
              argument_count = 1 : i64, has_target = true, node_id = 4 : i64,
              target = @leaf, target_path = "leaf"} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "7", is_signed = true, node_id = 5 : i64,
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "3", is_signed = true, node_id = 6 : i64,
              semantic_type = !int} {
          }
          obelisk.sv.rand_seq.item attributes {
              argument_count = 1 : i64, has_target = true, node_id = 7 : i64,
              target = @leaf, target_path = "leaf"} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "11", is_signed = true, node_id = 8 : i64,
                semantic_type = !int} {
            }
          }
        }
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 1 : i64,
            formal_arguments = [{default_operand_count = 0 : i64,
              direction = 0 : i32, referenced_path = "leaf.value",
              semantic_type = !int}],
            node_id = 9 : i64, referenced_path = "leaf",
            referenced_symbol = @leaf, rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
          obelisk.sv.rand_seq.code_block attributes {
              block_path = "leaf", node_id = 10 : i64} {
            obelisk.sv.statement.expression_statement attributes {
                node_id = 11 : i64} {
              obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 12 : i64,
                  referenced_path = "leaf.value",
                  referenced_symbol = @value, semantic_type = !int} {
              }
            }
          }
        }
      }
      obelisk_sim.return
    }

    obelisk_sim.func @controls(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [],
          code_unit_id = 2 : i64
        } {
      obelisk.sv.statement.rand_sequence attributes {
          first_production = @control_main, node_id = 20 : i64,
          production_count = 2 : i64} {
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 21 : i64,
            referenced_path = "control_main",
            referenced_symbol = @control_main, rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 4>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
          obelisk.sv.rand_seq.if_else attributes {
              has_else = true, node_id = 22 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", is_signed = true, node_id = 23 : i64,
                semantic_type = !int} {
            }
            obelisk.sv.rand_seq.item attributes {
                argument_count = 0 : i64, node_id = 24 : i64,
                target = @control_leaf} {
            }
            obelisk.sv.rand_seq.item attributes {
                argument_count = 0 : i64, node_id = 25 : i64,
                target = @control_leaf} {
            }
          }
          obelisk.sv.rand_seq.case attributes {
              has_default = true, item_count = 1 : i64,
              item_expression_counts = array<i64: 1>, node_id = 26 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", is_signed = true, node_id = 27 : i64,
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", is_signed = true, node_id = 28 : i64,
                semantic_type = !int} {
            }
            obelisk.sv.rand_seq.item attributes {
                argument_count = 0 : i64, node_id = 29 : i64,
                target = @control_leaf} {
            }
            obelisk.sv.rand_seq.item attributes {
                argument_count = 0 : i64, node_id = 30 : i64,
                target = @control_leaf} {
            }
          }
          obelisk.sv.rand_seq.repeat attributes {node_id = 31 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", is_signed = true, node_id = 32 : i64,
                semantic_type = !int} {
            }
            obelisk.sv.rand_seq.item attributes {
                argument_count = 0 : i64, node_id = 33 : i64,
                target = @control_leaf} {
            }
          }
          obelisk.sv.rand_seq.code_block attributes {node_id = 34 : i64} {
            obelisk.sv.statement.break attributes {node_id = 35 : i64} {
            }
          }
        }
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 36 : i64,
            referenced_path = "control_leaf",
            referenced_symbol = @control_leaf, rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
          obelisk.sv.rand_seq.code_block attributes {node_id = 37 : i64} {
            obelisk.sv.statement.return attributes {node_id = 38 : i64} {
            }
          }
        }
      }
      obelisk_sim.return
    }

    obelisk_sim.func @single_weight(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [],
          code_unit_id = 3 : i64
        } {
      obelisk.sv.statement.rand_sequence attributes {
          first_production = @single, node_id = 40 : i64,
          production_count = 2 : i64} {
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 41 : i64,
            referenced_path = "single", referenced_symbol = @single,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 1>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 0>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "1", is_signed = true, node_id = 42 : i64,
              semantic_type = !int} {
          }
        }
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 43 : i64,
            referenced_path = "dead", referenced_symbol = @dead,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
          obelisk.sv.rand_seq.item attributes {
              argument_count = 0 : i64, node_id = 44 : i64,
              target = @dead} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}

// Both signed weights are checked before they are accumulated. The total is
// also guarded against unsigned 64-bit overflow.
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK: arith.cmpi slt, %{{.*}}, %[[ZERO]]
// CHECK: arith.addi
// CHECK: arith.cmpi ult
// CHECK: arith.cmpi slt, %{{.*}}, %[[ZERO]]
// CHECK: %[[TOTAL:.*]] = arith.addi
// CHECK: arith.cmpi ult, %[[TOTAL]],

// A zero total bypasses rule selection; a nonzero total uses the process RNG.
// CHECK: %[[ANY:.*]] = arith.cmpi ne, %[[TOTAL]], %[[ZERO]]
// CHECK: cf.cond_br %[[ANY]], ^[[SELECT:[^ ,]*]], ^[[EXIT:[^ ,]*]]
// CHECK: ^[[SELECT]]:
// CHECK: %[[DRAW:.*]] = obelisk_sim.random.bounded %arg0, %[[TOTAL]]
// CHECK: arith.cmpi ult, %[[DRAW]],

// Each selected call allocates a fresh reference for the production formal.
// CHECK: %[[ARG0:.*]] = obelisk_sim.ref.alloc
// CHECK-NEXT: obelisk_sim.ref.load %[[ARG0]]
// CHECK: %[[ARG1:.*]] = obelisk_sim.ref.alloc
// CHECK-NEXT: obelisk_sim.ref.load %[[ARG1]]

// IEEE 1800-2017 18.17.2-.6 control productions lower to ordinary CFG. The
// selector/count are evaluated before their branches, production return joins
// only the current production, and break targets the whole randsequence.
// CHECK-LABEL: obelisk_sim.func @controls
// CHECK: arith.cmpi ne
// CHECK: cf.cond_br
// CHECK: arith.cmpi eq
// CHECK: cf.cond_br
// CHECK: ^{{.*}}(%[[INDEX:.*]]: i64):
// CHECK: arith.cmpi ult, %[[INDEX]],
// CHECK: cf.cond_br
// CHECK-NOT: obelisk.sv.rand_seq

// A sole explicit weight is evaluated when its production is selected, but
// 18.17.1 says a weight is meaningful only for alternative production lists.
// It therefore neither gates the sole rule nor consumes the process RNG.
// An unreachable recursive production does not impose an execution requirement
// on the selected top-level production.
// CHECK-LABEL: obelisk_sim.func @single_weight
// CHECK: arith.cmpi slt
// CHECK-NOT: arith.addi
// CHECK-NOT: obelisk_sim.random.bounded
