// RUN: not obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// Legal recursive grammars need dynamically nested production activations.
// Until those frames are outlined, diagnose the exact boundary instead of
// imposing an arbitrary recursion cutoff.

module {
  obelisk_sim.design @recursive {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "test.recursive"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    obelisk_sim.bindings = []} {
      obelisk.sv.statement.rand_sequence attributes {
          first_production = @p, node_id = 1 : i64,
          production_count = 1 : i64} {
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "p", referenced_symbol = @p,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 0>,
            rule_item_counts = array<i64: 1>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
          obelisk.sv.rand_seq.item attributes {
              argument_count = 0 : i64, node_id = 3 : i64, target = @p} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}

// CHECK: recursive randsequence production requires activation-frame lowering

// -----

// IEEE 1800-2017 18.17.6 defines rand join as depth-one random interleaving,
// not an arbitrary shuffle of the fully expanded production stream.

module {
  obelisk_sim.design @rand_join {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "test.rand_join"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    obelisk_sim.bindings = []} {
      obelisk.sv.statement.rand_sequence attributes {
          first_production = @p, node_id = 1 : i64,
          production_count = 1 : i64} {
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "p", referenced_symbol = @p,
            rule_count = 1 : i64,
            rule_has_rand_join_expressions = array<i64: 0>,
            rule_has_weight_code_blocks = array<i64: 0>,
            rule_has_weights = array<i64: 0>,
            rule_is_rand_join = array<i64: 1>,
            rule_item_counts = array<i64: 0>, rule_variables = [[]],
            semantic_type = !obelisk.void} {
        }
      }
      obelisk_sim.return
    }
  }
}

// CHECK: rand join requires depth-one interleaving lowering

// -----

// Value-returning productions also require per-rule implicit variables and
// expression-valued production calls, so reject that coherent boundary.

!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  obelisk_sim.design @production_values {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "test.values"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    obelisk_sim.bindings = []} {
      obelisk.sv.statement.rand_sequence attributes {
          first_production = @p, node_id = 1 : i64,
          production_count = 1 : i64} {
        obelisk.sv.rand_seq.frozen_production attributes {
            argument_count = 0 : i64, formal_arguments = [], node_id = 2 : i64,
            referenced_path = "p", referenced_symbol = @p,
            rule_count = 0 : i64,
            rule_has_rand_join_expressions = array<i64>,
            rule_has_weight_code_blocks = array<i64>,
            rule_has_weights = array<i64>, rule_is_rand_join = array<i64>,
            rule_item_counts = array<i64>, rule_variables = [],
            semantic_type = !int} {
        }
      }
      obelisk_sim.return
    }
  }
}

// CHECK: value-returning randsequence productions are outside the current executable boundary
