// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.LiveUnionMembers
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#long = #simulation.vpi_type<kind = longint, isSigned = true, isFourState = false,
    range = [63, 0], children = [], childNames = []>
#real = #simulation.vpi_type<kind = real, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#void = #simulation.vpi_type<kind = void, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#logic8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
    range = [7, 0], children = [], childNames = []>
#logic4 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
    range = [3, 0], children = [], childNames = []>
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false,
    range = [0, 0], children = [], childNames = []>
#flag_inner = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false,
    range = [0, 0], children = [#bit], childNames = []>
#flag = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false,
    range = [0, 1], children = [#flag_inner], childNames = []>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = false,
    name = "record_t", range = [], children = [#int], childNames = ["x"],
    isTagged = false, isSoft = false, bitWidth = 0 : i64,
    selectableWidth = 32 : i64, bitstreamWidth = 32 : i64, tagBits = 0 : i64,
    childOrdinals = [0], childPackedOffsets = [0]>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false,
    range = [2, 1], children = [#int], childNames = []>
#tagged = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "tagged_t", range = [], children = [#int, #string, #void, #record, #array],
    childNames = ["number", "text", "none", "record", "items"], isTagged = true,
    isSoft = false, bitWidth = 0 : i64, selectableWidth = 64 : i64,
    bitstreamWidth = 64 : i64, tagBits = 3 : i64,
    childOrdinals = [0, 1, 2, 3, 4], childPackedOffsets = [0, 0, 0, 0, 0]>
#union = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "union_t", range = [], children = [#long, #real],
    childNames = ["bits", "number"], isTagged = false, isSoft = false,
    bitWidth = 0 : i64, selectableWidth = 64 : i64,
    bitstreamWidth = 64 : i64, tagBits = 0 : i64,
    childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#packed = #simulation.vpi_type<kind = packed_union, isSigned = false, isFourState = true,
    name = "packed_t", range = [], children = [#logic8, #logic4, #flag],
    childNames = ["wide", "narrow", "flag"], isTagged = true, isSoft = false,
    bitWidth = 10 : i64, selectableWidth = 10 : i64,
    bitstreamWidth = 10 : i64, tagBits = 2 : i64,
    childOrdinals = [0, 1, 2], childPackedOffsets = [0, 0, 0]>
!record = !simulation.unpacked_struct<[
    #simulation.field<name = "x", type = i32, ordinal = 0, packedOffset = 0>]>
!array = !simulation.unpacked_array<2 : 1 x i32>
!tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>,
    #simulation.field<name = "none", type = i1, ordinal = 2, packedOffset = 0>,
    #simulation.field<name = "record", type = !record, ordinal = 3, packedOffset = 0>,
    #simulation.field<name = "items", type = !array, ordinal = 4, packedOffset = 0>], isTagged = true>
!union = !simulation.unpacked_union<fields = [
    #simulation.field<name = "bits", type = i64, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "number", type = f64, ordinal = 1, packedOffset = 0>], isTagged = false>
!packed = !simulation.packed_union<fields = [
    #simulation.field<name = "wide", type = !simulation.logic<8>, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "narrow", type = !simulation.logic<4>, ordinal = 1, packedOffset = 0>,
    #simulation.field<name = "flag", type = !simulation.packed_array<0 : 1 x !simulation.packed_array<0 : 0 x i1>>, ordinal = 2, packedOffset = 0>], isTagged = true, tagBits = 2>
#nibble = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false,
    range = [3, 0], children = [], childNames = []>
#small_tagged = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "small_tagged_t", range = [], children = [#nibble, #nibble], childNames = ["first", "second"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 4 : i64,
    bitstreamWidth = 4 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
!small_tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "first", type = i4, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "second", type = i4, ordinal = 1, packedOffset = 0>], isTagged = true>

#nested = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "nested_t", range = [], children = [#small_tagged, #int], childNames = ["inner", "number"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 32 : i64,
    bitstreamWidth = 32 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
!nested = !simulation.unpacked_union<fields = [
    #simulation.field<name = "inner", type = !small_tagged, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "number", type = i32, ordinal = 1, packedOffset = 0>], isTagged = true>
!port = !simulation.virtual_interface<"@bus", "">
!queue = !simulation.queue<!port, 0>
#port = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false,
    name = "@bus", symbol = @bus_type, modport = "", range = [], children = [], childNames = []>
#queue = #simulation.vpi_type<kind = queue, isSigned = false, isFourState = false,
    queueBound = 0 : i64, range = [], children = [#port], childNames = []>
#ports = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "ports_t", range = [], children = [#queue, #int], childNames = ["ports", "number"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 64 : i64,
    bitstreamWidth = 64 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
!ports = !simulation.unpacked_union<fields = [
    #simulation.field<name = "ports", type = !queue, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "number", type = i32, ordinal = 1, packedOffset = 0>], isTagged = true>

module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @live_unions {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.first" vpi_kind 601
    simulation.scope.decl 3 parent 1 hierarchy "top.second" vpi_kind 601
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @first id 1 type 601 in 2 parent @top ordinal 0 hierarchy "top.first" debug "first" {
      backing = #simulation.vpi_backing<kind = scope, id = 2 : i64>
    }
    simulation.vpi_object.anchor @second id 2 type 601 in 3 parent @top ordinal 1 hierarchy "top.second" debug "second" {
      backing = #simulation.vpi_backing<kind = scope, id = 3 : i64>
    }
    simulation.vpi_typespec.decl @bus_type id 0 in 1 owner @top hierarchy "@bus" debug "bus" {origin = 1 : i32, target_type = #port}
    simulation.storage.decl 0 in 1 : !tagged design hierarchy "top.tagged" {vpi_type = #tagged}
    simulation.storage.decl 1 in 1 : !packed design hierarchy "top.packed" {vpi_type = #packed}
    simulation.storage.decl 2 in 1 : !union design hierarchy "top.overlap" {vpi_type = #union}
    simulation.storage.decl 3 in 1 : i32 design hierarchy "top.physical_only"
    simulation.storage.decl 4 in 1 : !nested design hierarchy "top.nested" {vpi_type = #nested}
    simulation.storage.decl 5 in 1 : !ports design hierarchy "top.ports" {vpi_type = #ports}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.inspect"
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "inspect" callee 45
    simulation.vpi_local.decl @small id 1 in 45 scope 1 ordinal 0 hierarchy "top.inspect.small" debug "small" : !small_tagged {vpi_type = #small_tagged}
    simulation.func @entry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
  }
}
