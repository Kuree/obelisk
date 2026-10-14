// RUN: %split-file %s %t
// RUN: obelisk-opt %t/good.mlir --split-input-file --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit,obelisk-sim-compile-random-plans)))' | FileCheck %s --check-prefix=GOOD
// RUN: obelisk-opt %t/bad.mlir --split-input-file --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' -verify-diagnostics -o /dev/null
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/many.sv | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=MANY
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3: a semantic domain represents each legal value once, so
// the frozen domain schema of a randomized property must be disjoint. The two
// good inputs carry partial-mask cubes (a 4-bit and a 130-bit domain) that
// differ in a masked bit and must reach a sampling plan.
// GOOD: simulation.random.solve
// GOOD-SAME: SAMP
// GOOD: simulation.random.solve
// GOOD-SAME: SAMP

// A 256-value 130-bit enum has one equal-mask pattern per enumerator. The
// linear validation accepts it and the constrained randomize lowers to a wide
// mixed-radix solve over exactly 256 ranks.
// MANY: simulation.random.solve_wide
// MANY-SAME: %c256_i64

// Overlapping partial cubes, the same overlap with a hard-unsatisfiable inline
// constraint, wide overlapping cubes and a duplicated pattern are rejected
// independently of the hard constraints.

//--- good.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @units {
    simulation.code_unit.decl 900001 in 0 initial hierarchy "top.check"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i4 design hierarchy "top.x"
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x: !simulation.ref<i4> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, simulation.delay_scale = 1 : i64,
                    simulation.bindings = [#simulation.argument_binding<path = "top.x", argument = 1, kind = direct, copyOut = false>],
                    code_unit_id = 900001 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {
          argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          simulation.randomize, simulation.randomize_scope, simulation.randomize_explicit_properties,
          simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32,
          simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_recursive_alias_guards = [], simulation.random_total_width = 4 : i64,
          simulation.random_properties = [{is_randc = false, is_signed = false, rand_mode_index = 0 : i32, reference_path = "top.x", type = i4, width = 4 : i64,
            domains = [{offset = 0 : i64, width = 4 : i64, patterns = [
              {mask = -4 : i4, value = 4 : i4},
              {mask = -2 : i4, value = -6 : i4}
            ]}]}],
          subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @unused
        } {}
      }
      simulation.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @units {
    simulation.code_unit.decl 900001 in 0 initial hierarchy "top.check"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i130 design hierarchy "top.x"
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x: !simulation.ref<i130> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, simulation.delay_scale = 1 : i64,
                    simulation.bindings = [#simulation.argument_binding<path = "top.x", argument = 1, kind = direct, copyOut = false>],
                    code_unit_id = 900001 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {
          argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          simulation.randomize, simulation.randomize_scope, simulation.randomize_explicit_properties,
          simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32,
          simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_recursive_alias_guards = [], simulation.random_total_width = 130 : i64,
          simulation.random_properties = [{is_randc = false, is_signed = false, rand_mode_index = 0 : i32, reference_path = "top.x", type = i130, width = 130 : i64,
            domains = [{offset = 0 : i64, width = 130 : i64, patterns = [
              {mask = -4 : i130, value = 1267650600228229401496703205380 : i130},
              {mask = -2 : i130, value = 1267650600228229401496703205386 : i130}
            ]}]}],
          subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @unused
        } {}
      }
      simulation.return
    }
  }
}

//--- bad.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @units {
    simulation.code_unit.decl 900001 in 0 initial hierarchy "top.check"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i4 design hierarchy "top.x"
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x: !simulation.ref<i4> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, simulation.delay_scale = 1 : i64,
                    simulation.bindings = [#simulation.argument_binding<path = "top.x", argument = 1, kind = direct, copyOut = false>],
                    code_unit_id = 900001 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // expected-warning@+2 {{discards its return value}}
        // expected-error@+1 {{randomize property domain patterns overlap}}
        obelisk.sv.expression.call attributes {
          argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          simulation.randomize, simulation.randomize_scope, simulation.randomize_explicit_properties,
          simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32,
          simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_recursive_alias_guards = [], simulation.random_total_width = 4 : i64,
          simulation.random_properties = [{is_randc = false, is_signed = false, rand_mode_index = 0 : i32, reference_path = "top.x", type = i4, width = 4 : i64,
            domains = [{offset = 0 : i64, width = 4 : i64, patterns = [
              {mask = -4 : i4, value = 4 : i4},
              {mask = -2 : i4, value = 6 : i4}
            ]}]}],
          subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @unused
        } {}
      }
      simulation.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @units {
    simulation.code_unit.decl 900001 in 0 initial hierarchy "top.check"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i4 design hierarchy "top.x"
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x: !simulation.ref<i4> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, simulation.delay_scale = 1 : i64,
                    simulation.bindings = [#simulation.argument_binding<path = "top.x", argument = 1, kind = direct, copyOut = false>],
                    code_unit_id = 900001 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // expected-warning@+2 {{discards its return value}}
        // expected-error@+1 {{randomize property domain patterns overlap}}
        obelisk.sv.expression.call attributes {
          argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          simulation.randomize, simulation.randomize_scope, simulation.randomize_explicit_properties,
          simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32,
          simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_recursive_alias_guards = [], simulation.random_total_width = 4 : i64,
          simulation.random_properties = [{is_randc = false, is_signed = false, rand_mode_index = 0 : i32, reference_path = "top.x", type = i4, width = 4 : i64,
            domains = [{offset = 0 : i64, width = 4 : i64, patterns = [
              {mask = -4 : i4, value = 4 : i4},
              {mask = -2 : i4, value = 6 : i4}
            ]}]}],
          subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @unused
        } {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 3 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 4 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5 : i64} {}
            }
          }
        }
      }
      simulation.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @units {
    simulation.code_unit.decl 900001 in 0 initial hierarchy "top.check"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i130 design hierarchy "top.x"
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x: !simulation.ref<i130> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, simulation.delay_scale = 1 : i64,
                    simulation.bindings = [#simulation.argument_binding<path = "top.x", argument = 1, kind = direct, copyOut = false>],
                    code_unit_id = 900001 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // expected-warning@+2 {{discards its return value}}
        // expected-error@+1 {{randomize property domain patterns overlap}}
        obelisk.sv.expression.call attributes {
          argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          simulation.randomize, simulation.randomize_scope, simulation.randomize_explicit_properties,
          simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32,
          simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_recursive_alias_guards = [], simulation.random_total_width = 130 : i64,
          simulation.random_properties = [{is_randc = false, is_signed = false, rand_mode_index = 0 : i32, reference_path = "top.x", type = i130, width = 130 : i64,
            domains = [{offset = 0 : i64, width = 130 : i64, patterns = [
              {mask = -4 : i130, value = 1267650600228229401496703205380 : i130},
              {mask = -2 : i130, value = 1267650600228229401496703205382 : i130}
            ]}]}],
          subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @unused
        } {}
      }
      simulation.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @units {
    simulation.code_unit.decl 900001 in 0 initial hierarchy "top.check"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i4 design hierarchy "top.x"
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x: !simulation.ref<i4> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, simulation.delay_scale = 1 : i64,
                    simulation.bindings = [#simulation.argument_binding<path = "top.x", argument = 1, kind = direct, copyOut = false>],
                    code_unit_id = 900001 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // expected-warning@+2 {{discards its return value}}
        // expected-error@+1 {{randomize property domain patterns overlap}}
        obelisk.sv.expression.call attributes {
          argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          simulation.randomize, simulation.randomize_scope, simulation.randomize_explicit_properties,
          simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32,
          simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_recursive_alias_guards = [], simulation.random_total_width = 4 : i64,
          simulation.random_properties = [{is_randc = false, is_signed = false, rand_mode_index = 0 : i32, reference_path = "top.x", type = i4, width = 4 : i64,
            domains = [{offset = 0 : i64, width = 4 : i64, patterns = [
              {mask = -4 : i4, value = 4 : i4},
              {mask = -4 : i4, value = 4 : i4}
            ]}]}],
          subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @unused
        } {}
      }
      simulation.return
    }
  }
}

//--- many.sv
module top;
  typedef enum logic [129:0] { E[256] = 130'h10_0000_0000_0000_0000_0000_0000 } many_t;
  many_t m;
  initial void'(std::randomize(m) with { m != E0; });
endmodule
