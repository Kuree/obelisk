// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CompiledResolutionProbes
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LLVM
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @resolution_frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve" debug "resolve"
    simulation.vpi_object.anchor @resolver id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design hierarchy "top.n" debug "n" {
      nettype = @nt,
      vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 1 in 1 : !simulation.unpacked_array<3 : 0 x !simulation.logic<2>> design hierarchy "top.array_n" debug "array_n" {
      nettype = @nt,
      vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>,
      vpi_type = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = true, range = [3, 0], children = [#simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>], childNames = []>
    }
    simulation.net.decl 2 in 1 : !simulation.logic<2> design hierarchy "top.wire" debug "wire" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.statement.decl 11 in 45 scope 1 type 3
    simulation.statement_site.decl 110 on 11 phase 0
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %actor = simulation.spawn @worker(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @worker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      %phase0 = arith.constant 0 : i32
      %ticket0 = simulation.vpi.resolution.prepare %ctx net 0
      simulation.call @resolve(%ctx, %phase0) {simulation.vpi_resolution_net = 0 : i64} : (!simulation.context, i32) -> ()
      simulation.vpi.call.finish %ctx, %ticket0
      %phase1 = arith.constant 1 : i32
      %ticket1 = simulation.vpi.resolution.prepare %ctx net 1
      simulation.call @resolve(%ctx, %phase1) {simulation.vpi_resolution_net = 1 : i64} : (!simulation.context, i32) -> ()
      simulation.vpi.call.finish %ctx, %ticket1
      %phase2 = arith.constant 2 : i32
      %status2 = simulation.dpi.call "inspect_resolution" id 123 scope 1 context %ctx : !simulation.context(%phase2) {
        abi_signature = [#dpi.abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>],
        is_context = true, is_pure = false, is_task = false,
        source_column = 1 : i32, source_file = "resolution-probes.mlir", source_line = 1 : i32
      } : (i32) -> !runtime.status
      simulation.return
    }
    simulation.func private @resolve(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %phase0: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64} {
      %frame = simulation.vpi.frame.enter_prepared %ctx unit 45
      simulation.vpi.frame.checkpoint %ctx statement 11
      %status0 = simulation.dpi.call "inspect_resolution" id 123 scope 1 context %ctx : !simulation.context(%phase0) {
        abi_signature = [#dpi.abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>],
        is_context = true, is_pure = false, is_task = false,
        source_column = 1 : i32, source_file = "resolution-probes.mlir", source_line = 1 : i32
      } : (i32) -> !runtime.status
      simulation.vpi.frame.leave %ctx, %frame
      simulation.return
    }
  }
}
// BYTECODE: id=0x00011017 inputs=1 outputs=1 flags=0
// LLVM: llvm.call @obelisk_rt_v1_vpi_resolution_prepare
// LLVM: llvm.call @obelisk_rt_v1_vpi_resolution_prepare
// LLVM: llvm.call @obelisk_rt_v1_vpi_frame_enter_prepared
// LLVM-NOT: simulation.vpi.
