// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.DPIExportFrames
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @export_frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.imported" debug "imported"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.exported" debug "exported"
    simulation.code_unit.decl 47 in 1 function hierarchy "top.nested" debug "nested"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 46 scope 1 type 19
    simulation.vpi_call.decl @outer id 20 in 44 scope 1 type 19 name "imported" statement 11 within 11 callee 45
    simulation.vpi_call.decl @inner id 21 in 46 scope 1 type 19 name "imported" statement 12 within 12 callee 45
    simulation.func @entry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
    simulation.func private @imported(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64} {
      simulation.return
    }
    simulation.func private @exported(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
    simulation.func private @nested(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 47 : i64} {
      simulation.return
    }
  }
}
