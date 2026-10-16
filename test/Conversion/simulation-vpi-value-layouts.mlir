// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ExactVPIElementDescriptors
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#logic = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [12, 0], children = [], childNames = []>
#real = #simulation.vpi_type<kind = real, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
#semaphore = #simulation.vpi_type<kind = semaphore, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
#mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false, range = [], children = [#int], childNames = []>
#dynamic = #simulation.vpi_type<kind = dynamic_array, isSigned = false, isFourState = false, range = [], children = [#string], childNames = []>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = true,
    name = "record_t", range = [], children = [#logic, #string], childNames = ["bits", "text"],
    isTagged = false, isSoft = false, bitWidth = 0 : i64, selectableWidth = 77 : i64,
    bitstreamWidth = 77 : i64, tagBits = 0 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#overlap = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "overlap_t", range = [], children = [#string, #semaphore], childNames = ["text", "lock"],
    isTagged = false, isSoft = false, bitWidth = 0 : i64, selectableWidth = 64 : i64,
    bitstreamWidth = 64 : i64, tagBits = 0 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#tagged = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "tagged_t", range = [], children = [#string, #semaphore], childNames = ["text", "lock"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 64 : i64,
    bitstreamWidth = 64 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = true, range = [2, 1], children = [#record], childNames = []>
#enum = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false, name = "enum_t", range = [], children = [#int], childNames = []>
#alias_enum = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false, name = "enum_t", range = [], children = [#int], childNames = [], typedefAliases = [@alias]>
!record = !simulation.unpacked_struct<[
    #simulation.field<name = "bits", type = !simulation.logic<13>, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>]>
!overlap = !simulation.unpacked_union<fields = [
    #simulation.field<name = "text", type = !simulation.string, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "lock", type = !simulation.semaphore, ordinal = 1, packedOffset = 0>], isTagged = false>
!tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "text", type = !simulation.string, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "lock", type = !simulation.semaphore, ordinal = 1, packedOffset = 0>], isTagged = true>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @value_layouts {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @alias id 0 in 1 owner @top hierarchy "top.enum_t" debug "enum_t" {
      origin = 0 : i32, target_type = #enum}
    simulation.vpi_box_type.decl @enum id 12 : i32 {vpi_type = #alias_enum}
    simulation.vpi_box_type.decl @integer id 1 : i32 {vpi_type = #int}
    simulation.vpi_box_type.decl @logic id 2 : !simulation.logic<13> {vpi_type = #logic}
    simulation.vpi_box_type.decl @real id 3 : f64 {vpi_type = #real}
    simulation.vpi_box_type.decl @string id 4 : !simulation.string {vpi_type = #string}
    simulation.vpi_box_type.decl @semaphore id 5 : !simulation.semaphore {vpi_type = #semaphore}
    simulation.vpi_box_type.decl @mailbox id 6 : !simulation.mailbox<i32> {vpi_type = #mailbox}
    simulation.vpi_box_type.decl @dynamic id 7 : !simulation.dynamic_array<!simulation.string> {vpi_type = #dynamic}
    simulation.vpi_box_type.decl @record id 8 : !record {vpi_type = #record}
    simulation.vpi_box_type.decl @overlap id 9 : !overlap {vpi_type = #overlap}
    simulation.vpi_box_type.decl @tagged id 10 : !tagged {vpi_type = #tagged}
    simulation.vpi_box_type.decl @array id 11 : !simulation.unpacked_array<2 : 1 x !record> {vpi_type = #array}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
