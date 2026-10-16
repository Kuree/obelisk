// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.FrameCallProtocol:GeneratedDesignDatabase.FrameCallActionContract
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @frame_calls {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.outer"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.middle"
    simulation.code_unit.decl 46 in 1 task hierarchy "top.inner"
    simulation.code_unit.decl 47 in 1 function hierarchy "top.helper"
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement.decl 12 in 45 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 13 in 45 scope 1 type 60 parent 12
    simulation.statement.decl 14 in 46 scope 1 type 3
    simulation.statement.decl 15 in 47 scope 1 type 38
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.statement_site.decl 1200 on 12 phase 0
    simulation.statement_site.decl 1300 on 13 phase 0
    simulation.statement_site.decl 1400 on 14 phase 0
    simulation.vpi_call.decl @middle_call id 20 in 44 scope 1 type 60
        name "middle" statement 11 within 11 callee 45
    simulation.vpi_call.decl @inner_call id 21 in 45 scope 1 type 60
        name "inner" statement 13 within 13 callee 46
    simulation.vpi_call.decl @helper_call id 22 in 46 scope 1 type 19
        name "helper" within 14 callee 47
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @outer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @outer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase0) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 1 : i32
          } : (i32) -> !runtime.status
      simulation.task.call @middle(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      %phase8 = arith.constant 8 : i32
      %status8 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase8) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 9 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
    simulation.func private @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 45 : i64} {
      %phase1 = arith.constant 1 : i32
      %status1 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase1) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 2 : i32
          } : (i32) -> !runtime.status
      %block = simulation.control.enter 777
      simulation.control.boundary %block resume ^exit body ^body
    ^body:
      %phase2 = arith.constant 2 : i32
      %status2 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase2) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 3 : i32
          } : (i32) -> !runtime.status
      simulation.task.call @inner(%ctx, %block) arguments 1 to ^done : !simulation.context, !simulation.control
    ^done(%resumedBlock: !simulation.control):
      %phase6 = arith.constant 6 : i32
      %status6 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase6) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 7 : i32
          } : (i32) -> !runtime.status
      simulation.control.leave %resumedBlock
      cf.br ^exit
    ^exit:
      %phase7 = arith.constant 7 : i32
      %status7 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase7) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 8 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
    simulation.func private @inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 46 : i64} {
      %phase3 = arith.constant 3 : i32
      %status3 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase3) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 4 : i32
          } : (i32) -> !runtime.status
      simulation.call @helper(%ctx) : (!simulation.context) -> ()
      %phase11 = arith.constant 11 : i32
      %status11 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase11) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 12 : i32
          } : (i32) -> !runtime.status
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^resume
    ^resume:
      %phase4 = arith.constant 4 : i32
      %status4 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase4) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 5 : i32
          } : (i32) -> !runtime.status
      %escape = simulation.control.escape_pending
      cf.cond_br %escape, ^exit, ^normal
    ^normal:
      %phase5 = arith.constant 5 : i32
      %status5 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase5) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 6 : i32
          } : (i32) -> !runtime.status
      cf.br ^exit
    ^exit:
      simulation.return
    }
    simulation.func private @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 47 : i64} {
      %phase10 = arith.constant 10 : i32
      %status10 = simulation.dpi.call "inspect_frame_calls" id 123 scope 1
          context %ctx : !simulation.context(%phase10) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-calls.mlir",
            source_line = 11 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
  }
}
