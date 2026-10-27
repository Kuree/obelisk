// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.FrameQueriesFollowActivationLifetime
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.worker"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.inner"
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement.decl 12 in 45 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 13 in 45 scope 1 type 3 parent 12
    simulation.statement.decl 14 in 46 scope 1 type 38
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.statement_site.decl 1200 on 12 phase 0
    simulation.statement_site.decl 1300 on 13 phase 0
    simulation.vpi_call.decl @task_call id 20 in 44 scope 1 type 60 name "worker" statement 11 within 11 callee 45
    simulation.vpi_call.decl @nested_call id 21 in 45 scope 1 type 19 name "inner" within 13 callee 46
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @outer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @outer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_frames" id 123 scope 1
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
      %status1 = simulation.dpi.call "inspect_frames" id 123 scope 1
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
