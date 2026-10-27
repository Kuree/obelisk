// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.DPIExportTaskFrames*
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @export_task_frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @imported id 1 type 59 in 1 parent @top ordinal 0 hierarchy "top.imported" debug "imported"
    simulation.vpi_object.anchor @event id 2 type 34 in 1 parent @top ordinal 1 hierarchy "top.ready" debug "ready"
    simulation.vpi_event_binding @event id 7
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.imported" {internal, dpi.import, dpi.task}
    simulation.vpi_code_unit_scope.bind 45 to @imported
    simulation.code_unit.decl 46 in 1 task hierarchy "top.exported" debug "exported"
    simulation.code_unit.decl 47 in 1 task hierarchy "top.nested" debug "nested"
    simulation.code_unit.decl 48 in 1 initial hierarchy "top.driver"
    simulation.code_unit.decl 49 in 1 initial hierarchy "top.child"
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement.decl 12 in 46 scope 1 type 3
    simulation.statement.decl 13 in 46 scope 1 type 60
    simulation.statement_site.decl 1300 on 13 phase 0
    simulation.vpi_call.decl @nested_call id 21 in 46 scope 1 type 60 name "nested" statement 13 within 13 callee 47
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.statement_site.decl 1200 on 12 phase 0
    simulation.vpi_call.decl @outer id 20 in 44 scope 1 type 60 name "imported" statement 11 within 11 callee 45
    simulation.func @entry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
    simulation.func private @imported_fn(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64} {
      simulation.return
    }
    simulation.func private @exported(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 46 : i64} {
      %activation = simulation.vpi.frame.enter_prepared %ctx unit 46 {is_task = true}
      simulation.vpi.frame.checkpoint %ctx statement 12
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_export_task" id 124 scope 1
          context %ctx : !simulation.context(%phase0, %activation) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #dpi.abi<kind = longint, direction = input, width = 64, fourState = false, isSigned = false>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "export-task-frames.mlir", source_line = 1 : i32
          } : (i32, i64) -> !runtime.status
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^resume(%activation : i64)
    ^resume(%saved: i64):
      %phase1 = arith.constant 1 : i32
      %status1 = simulation.dpi.call "inspect_export_task" id 124 scope 1
          context %ctx : !simulation.context(%phase1, %saved) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #dpi.abi<kind = longint, direction = input, width = 64, fourState = false, isSigned = false>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "export-task-frames.mlir", source_line = 1 : i32
          } : (i32, i64) -> !runtime.status
      simulation.vpi.frame.leave %ctx, %saved
      simulation.return
    }
  }
}
