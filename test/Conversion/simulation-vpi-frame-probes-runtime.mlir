// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CompiledFrameProbes:GeneratedDesignDatabase.FrameProbeSignaturesRejectNonIdentityLayouts
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.worker"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.inner"
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement.decl 12 in 45 scope 1 type 19
    simulation.statement.decl 13 in 46 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 14 in 46 scope 1 type 38 parent 13
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.statement_site.decl 1300 on 13 phase 0
    simulation.vpi_call.decl @task_call id 20 in 44 scope 1 type 60 name "worker" statement 11 within 11 callee 45
    simulation.vpi_call.decl @function_call id 21 in 45 scope 1 type 19 name "inner" statement 12 within 12 callee 46
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @outer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @outer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %root = simulation.vpi.frame.enter %ctx unit 44
      simulation.vpi.frame.checkpoint %ctx statement 11
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_compiled_frames" id 123 scope 1
          context %ctx : !simulation.context(%phase0) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-probes.mlir", source_line = 1 : i32
          } : (i32) -> !runtime.status
      %ticket = simulation.vpi.call.prepare %ctx from @task_call
      simulation.task.call @worker(%ctx, %root, %ticket) arguments 1 to ^done : !simulation.context, i64, i64
    ^done(%resumed_root: i64, %resumed_ticket: i64):
      simulation.vpi.call.finish %ctx, %resumed_ticket
      %phase4 = arith.constant 4 : i32
      %status4 = simulation.dpi.call "inspect_compiled_frames" id 123 scope 1
          context %ctx : !simulation.context(%phase4) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-probes.mlir", source_line = 5 : i32
          } : (i32) -> !runtime.status
      simulation.vpi.frame.leave %ctx, %resumed_root
      %phase5 = arith.constant 5 : i32
      %status5 = simulation.dpi.call "inspect_compiled_frames" id 123 scope 1
          context %ctx : !simulation.context(%phase5) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-probes.mlir", source_line = 6 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
    simulation.func private @worker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 45 : i64} {
      %task = simulation.vpi.frame.enter_prepared %ctx unit 45 {is_task = true}
      simulation.vpi.frame.checkpoint %ctx statement 12
      %phase1 = arith.constant 1 : i32
      %status1 = simulation.dpi.call "inspect_compiled_frames" id 123 scope 1
          context %ctx : !simulation.context(%phase1) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-probes.mlir", source_line = 2 : i32
          } : (i32) -> !runtime.status
      %ticket = simulation.vpi.call.prepare %ctx from @function_call
      simulation.call @inner(%ctx) : (!simulation.context) -> ()
      simulation.vpi.call.finish %ctx, %ticket
      %phase3 = arith.constant 3 : i32
      %status3 = simulation.dpi.call "inspect_compiled_frames" id 123 scope 1
          context %ctx : !simulation.context(%phase3) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-probes.mlir", source_line = 4 : i32
          } : (i32) -> !runtime.status
      simulation.vpi.frame.leave %ctx, %task
      simulation.return
    }
    simulation.func private @inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      %function = simulation.vpi.frame.enter_prepared %ctx unit 46
      %block = simulation.vpi.frame.enter %ctx unit 46 within 13
      simulation.vpi.frame.checkpoint %ctx statement 14
      %phase2 = arith.constant 2 : i32
      %status2 = simulation.dpi.call "inspect_compiled_frames" id 123 scope 1
          context %ctx : !simulation.context(%phase2) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "frame-probes.mlir", source_line = 3 : i32
          } : (i32) -> !runtime.status
      simulation.vpi.frame.leave %ctx, %block
      simulation.vpi.frame.leave %ctx, %function
      simulation.return
    }
  }
}
