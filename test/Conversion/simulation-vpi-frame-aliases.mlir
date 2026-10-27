// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.FrameReferenceAliases
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#flags = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [8, 0], children = [], childNames = []>
#nibble = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false,
    range = [3, 0], children = [], childNames = []>
#small_tagged = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "small_tagged_t", range = [], children = [#nibble, #nibble], childNames = ["first", "second"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 4 : i64,
    bitstreamWidth = 4 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
!small_tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "first", type = i4, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "second", type = i4, ordinal = 1, packedOffset = 0>], isTagged = true>

#semaphore = #simulation.vpi_type<kind = semaphore, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @aliases {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i64 design hierarchy "top.backing"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.inspect"
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "inspect" callee 45
    simulation.vpi_local.decl @value id 1 in 45 scope 1 ordinal 0 hierarchy "top.inspect.value" debug "value" : i32 {vpi_type = #word}
    simulation.vpi_local.decl @flags id 2 in 45 scope 1 ordinal 1 hierarchy "top.inspect.flags" debug "flags" : !simulation.logic<9> {vpi_type = #flags}
    simulation.vpi_local.decl @lock id 3 in 45 scope 1 ordinal 2 hierarchy "top.inspect.lock" debug "lock" : !simulation.semaphore {vpi_type = #semaphore}
    simulation.vpi_local.decl @tagged id 4 in 45 scope 1 ordinal 3 hierarchy "top.inspect.tagged" debug "tagged" : !small_tagged {vpi_type = #small_tagged}
    simulation.func @entry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
  }
}
