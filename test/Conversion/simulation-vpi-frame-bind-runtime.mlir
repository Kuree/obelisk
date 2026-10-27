// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.CompiledFrameLocalBinding
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.result" {vpi_type = #word}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.statement.decl 11 in 44 scope 1 type 33 name "block" {is_scope}
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.vpi_local.decl @local id 19 in 44 scope 1 within 11 ordinal 0 hierarchy "top.block.value" debug "value" : i32 {vpi_type = #word}
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @actor(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @actor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %phase0 = arith.constant 0 : i32
      %status0 = simulation.dpi.call "inspect_compiled_binding" id 123 scope 1
          context %ctx : !simulation.context(%phase0) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "compiled-local.mlir",
            source_line = 1 : i32
          } : (i32) -> !runtime.status
      %value = arith.constant 23 : i32
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      simulation.vpi.frame.bind %ctx, %local to @local : !simulation.ref<i32>
      %phase1 = arith.constant 1 : i32
      %status1 = simulation.dpi.call "inspect_compiled_binding" id 123 scope 1
          context %ctx : !simulation.context(%phase1) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "compiled-local.mlir",
            source_line = 2 : i32
          } : (i32) -> !runtime.status
      %replacement = arith.constant 47 : i32
      simulation.ref.store %replacement to %local : i32, !simulation.ref<i32>
      %phase2 = arith.constant 2 : i32
      %status2 = simulation.dpi.call "inspect_compiled_binding" id 123 scope 1
          context %ctx : !simulation.context(%phase2) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "compiled-local.mlir",
            source_line = 3 : i32
          } : (i32) -> !runtime.status
      // The actual later source read keeps this reference live through both
      // observation callbacks; the VPI binding itself does not own storage.
      %current = simulation.ref.load %local : !simulation.ref<i32> -> i32
      %output = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %current to %output : i32, !simulation.ref<i32>
      %phase3 = arith.constant 3 : i32
      %status3 = simulation.dpi.call "inspect_compiled_binding" id 123 scope 1
          context %ctx : !simulation.context(%phase3) {
            abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "compiled-local.mlir",
            source_line = 4 : i32
          } : (i32) -> !runtime.status
      simulation.return
    }
  }
}
