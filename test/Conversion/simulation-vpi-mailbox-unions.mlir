// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.MailboxUnionMessages

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
#tagged_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#tagged], childNames = []>
#union_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#union], childNames = []>
#packed_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#packed], childNames = []>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @mailbox_unions {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    simulation.vpi_typespec.decl @tagged id 0 in 0 owner @std
        hierarchy "std::tagged$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #tagged_mailbox, message_type = !tagged}
    simulation.storage.decl 0 in 1 : !simulation.mailbox<!tagged> design hierarchy "top.tagged" {
      vpi_type = #tagged_mailbox}
    simulation.vpi_typespec.decl @plain id 1 in 0 owner @std
        hierarchy "std::plain$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #union_mailbox, message_type = !union}
    simulation.storage.decl 1 in 1 : !simulation.mailbox<!union> design hierarchy "top.plain" {
      vpi_type = #union_mailbox}
    simulation.vpi_typespec.decl @packed id 2 in 0 owner @std
        hierarchy "std::packed$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #packed_mailbox, message_type = !packed}
    simulation.storage.decl 2 in 1 : !simulation.mailbox<!packed> design hierarchy "top.packed" {
      vpi_type = #packed_mailbox}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
