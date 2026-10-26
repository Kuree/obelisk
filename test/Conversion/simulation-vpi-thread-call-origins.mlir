// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ThreadOriginKeepsOutermostBytecodeActivation
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @thread_call_origins {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.outer"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.middle"
    simulation.code_unit.decl 46 in 1 task hierarchy "top.inner"
    simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @event id 1 type 34 in 1 parent @module ordinal 0
        hierarchy "top.ready" debug "ready"
    simulation.vpi_event_binding @event id 7
    simulation.statement.decl 440 in 44 scope 1 type 33 name "outer_body" {is_scope}
    simulation.statement_site.decl 4400 on 440 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 440
    simulation.vpi_thread_origin_binding 44 origin 440
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @outer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @outer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^ready
    ^ready:
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_thread_origin" id 123 scope 1
          context %ctx : !simulation.context(%phase0) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "thread-origins.mlir",
            source_line = 1 : i32
          } : (i32) -> !runtime.status
      simulation.task.call @middle(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      %phase3 = arith.constant 3 : i32
      %status3 = simulation.dpi.call "inspect_thread_origin" id 123 scope 1
          context %ctx : !simulation.context(%phase3) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "thread-origins.mlir",
            source_line = 4 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
    simulation.func private @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 45 : i64} {
      %phase1 = arith.constant 1 : i32
      %status1 = simulation.dpi.call "inspect_thread_origin" id 123 scope 1
          context %ctx : !simulation.context(%phase1) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "thread-origins.mlir",
            source_line = 2 : i32
          } : (i32) -> !runtime.status
      simulation.task.call @inner(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      %phase4 = arith.constant 4 : i32
      %status4 = simulation.dpi.call "inspect_thread_origin" id 123 scope 1
          context %ctx : !simulation.context(%phase4) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "thread-origins.mlir",
            source_line = 5 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
    simulation.func private @inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 46 : i64} {
      %phase2 = arith.constant 2 : i32
      %status2 = simulation.dpi.call "inspect_thread_origin" id 123 scope 1
          context %ctx : !simulation.context(%phase2) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "thread-origins.mlir",
            source_line = 3 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
  }
}
