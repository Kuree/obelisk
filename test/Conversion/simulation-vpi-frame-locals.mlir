// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=full},talos-sim-verify-compute-graph,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LLVM
// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.FrameAutomaticVariables
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [31, 0], children = [], childNames = []>
#flags = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = true, range = [2, 1], children = [#flags], childNames = []>
#semaphore = #simulation.vpi_type<kind = semaphore, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.scratch" {vpi_type = #word}
    simulation.vpi_local.decl @value id 1 in 45 scope 1 ordinal 0 hierarchy "top.worker.value" debug "value" : i32 {vpi_type = #word}
    simulation.vpi_local.decl @flags id 2 in 45 scope 1 ordinal 1 hierarchy "top.worker.flags" debug "flags" : !simulation.unpacked_array<2 : 1 x !simulation.logic<8>> {vpi_type = #array}
    simulation.vpi_local.decl @lock id 3 in 45 scope 1 ordinal 2 hierarchy "top.worker.lock" debug "lock" : !simulation.semaphore {vpi_type = #semaphore}
    simulation.vpi_local.decl @shadow id 4 in 45 scope 1 within 12 ordinal 0 hierarchy "top.worker.block.value" debug "value" : i32 {vpi_type = #word}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.worker"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.inner"
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement.decl 12 in 45 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 13 in 45 scope 1 type 3 parent 12
    simulation.statement.decl 14 in 46 scope 1 type 38
    simulation.vpi_expression.decl @sum id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 13 : i64> selector 82 handle ordinal 0 : i32 {vpi_type = #word, op_type = 24 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 13 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @sum>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @sum> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value> {simulation.vpi_expression_identifier = "value"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @sum> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @shadow> {simulation.vpi_expression_identifier = "value"}
    simulation.vpi_expression.decl @selected id 101 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 13 : i64> selector 77 handle ordinal 0 : i32 {vpi_type = #bits}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 13 : i64> selector 77 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @selected>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @selected> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @shadow>
    simulation.vpi_expression.decl @selected_left id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @selected> selector 79 handle ordinal 0 : i32 {vpi_type = #word, immutable_value = #simulation.frozen_constant<value = [31 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @selected> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @selected_left>
    simulation.vpi_expression.decl @selected_right id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @selected> selector 83 handle ordinal 0 : i32 {vpi_type = #word, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @selected> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @selected_right>
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.statement_site.decl 1200 on 12 phase 0
    simulation.statement_site.decl 1300 on 13 phase 0
    simulation.vpi_call.decl @task_call id 20 in 44 scope 1 type 60 name "worker" statement 11 within 11 callee 45
    simulation.vpi_call.decl @nested_call id 21 in 45 scope 1 type 19 name "inner" within 13 callee 46
    simulation.vpi_call.decl @recursive_call id 22 in 45 scope 1 type 60 name "worker" within 13 callee 45
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @outer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @outer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_locals" id 123 scope 1
          context %ctx : !simulation.context(%phase0) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-queries.mlir",
            source_line = 1 : i32
          } : (i32) -> !runtime.status
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^resume
    ^resume:
      %phase1 = arith.constant 1 : i32
      %status1 = simulation.dpi.call "inspect_locals" id 123 scope 1
          context %ctx : !simulation.context(%phase1) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-queries.mlir",
            source_line = 2 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
  }
}

// LLVM-NOT: simulation.vpi_local
// LLVM: llvm.func
// LLVM-NOT: simulation.vpi_local
