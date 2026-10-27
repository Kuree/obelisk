// RUN: not talos-opt %s --split-input-file --pass-pipeline='builtin.module(talos-sim-prepare-unit-lowering,simulation.design(simulation.func(talos-sim-lower-unit)))' 2>&1 | FileCheck %s

// A recursive production is emitted once, so a ref argument, which aliases a
// different actual in each activation, has no slot to live in.

module {
  simulation.design @recursive_ref {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.recursive_ref"
    simulation.scope.decl 0
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = []} {
      talos.sv.statement.rand_sequence attributes {
          first_production = @p, node_id = 1 : i64,
          production_count = 1 : i64} {
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 1 : i64,
            formal_arguments = [{default_operand_count = 0 : i64,
              direction = 3 : i32, referenced_path = "p.x",
              semantic_type = !talos.integral<32, true, false, 31 : 0, int>}],
            node_id = 2 : i64, referenced_path = "p", referenced_symbol = @p,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !talos.void} {
          talos.sv.rand_seq.item attributes {
              argument_count = 0 : i64, node_id = 3 : i64, target = @p} {
          }
        }
      }
      simulation.return
    }
  }
}

// CHECK: a recursive randsequence production cannot have a ref argument

// -----

// IEEE 1800-2017 18.17.5 requires two or more production items in a rand join.

module {
  simulation.design @rand_join {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.rand_join"
    simulation.scope.decl 0
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = []} {
      talos.sv.statement.rand_sequence attributes {
          first_production = @p, node_id = 1 : i64,
          production_count = 1 : i64} {
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "p", referenced_symbol = @p,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 1>,
            rule_item_counts = array<i64: 0>, rule_variables = [[]],
            semantic_type = !talos.void} {
        }
      }
      simulation.return
    }
  }
}

// CHECK: rand join requires at least two production items

// -----

// A nonvoid production must return an expression, as required by 18.17.7.

!int = !talos.integral<32, true, false, 31 : 0, int>

module {
  simulation.design @production_values {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.values"
    simulation.scope.decl 0
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = []} {
      talos.sv.statement.rand_sequence attributes {
          first_production = @p, node_id = 1 : i64,
          production_count = 1 : i64} {
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "p", referenced_symbol = @p,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !int} {
          talos.sv.rand_seq.code_block attributes {
              block_path = "p", node_id = 3 : i64} {
            talos.sv.statement.return attributes {node_id = 4 : i64} {
            }
          }
        }
      }
      simulation.return
    }
  }
}

// CHECK: randsequence return must match the production return type

// -----

// Repeated production results have the LRM range [1:N].
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
  simulation.design @result_range {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.result_range"
    simulation.scope.decl 0
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = []} {
      talos.sv.statement.rand_sequence attributes {
          first_production = @main, node_id = 1 : i64,
          production_count = 2 : i64} {
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "main", referenced_symbol = @main,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 2>,
            rule_variables = [[{name = "leaf", referenced_path = "main.leaf",
                semantic_type = !talos.ranged_unpacked_array<0 : 1 x !int>}]],
            semantic_type = !talos.void} {
          talos.sv.rand_seq.item attributes {
              argument_count = 0 : i64, node_id = 3 : i64, target = @leaf} {
          }
          talos.sv.rand_seq.item attributes {
              argument_count = 0 : i64, node_id = 4 : i64, target = @leaf} {
          }
        }
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 5 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf,
            rule_count = 0 : i64,
            rule_has_rand_join_expressions = array<i64>,
            rule_has_weight_code_blocks = array<i64>,
            rule_has_weights = array<i64>, rule_is_rand_join = array<i64>,
            rule_item_counts = array<i64>, rule_variables = [],
            semantic_type = !int} {
        }
      }
      simulation.return
    }
  }
}
// CHECK: rule return array must use [1:N]

// -----

// A ref argument must denote variable storage, as with an ordinary task.
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
  simulation.design @ref_literal {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.ref_literal"
    simulation.scope.decl 0
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = []} {
      talos.sv.statement.rand_sequence attributes {
          first_production = @main, node_id = 1 : i64,
          production_count = 2 : i64} {
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "main", referenced_symbol = @main,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !talos.void} {
          talos.sv.rand_seq.item attributes {
              argument_count = 1 : i64, node_id = 3 : i64, target = @leaf} {
            talos.sv.expression.integer_literal attributes {
                constant_value = "1", is_signed = true, node_id = 4 : i64,
                semantic_type = !int} {
            }
          }
        }
        talos.sv.rand_seq.frozen_production attributes {
            argument_count = 1 : i64,
            formal_arguments = [{default_operand_count = 0 : i64,
                direction = 3 : i32, referenced_path = "leaf.r",
                semantic_type = !int}], node_id = 5 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf,
            rule_count = 0 : i64,
            rule_has_rand_join_expressions = array<i64>,
            rule_has_weight_code_blocks = array<i64>,
            rule_has_weights = array<i64>, rule_is_rand_join = array<i64>,
            rule_item_counts = array<i64>, rule_variables = [],
            semantic_type = !talos.void} {
        }
      }
      simulation.return
    }
  }
}
// CHECK: randsequence ref actual must be an equivalent variable reference
