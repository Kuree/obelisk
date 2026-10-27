// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.MailboxElementLayouts

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false,
    range = [3, 0], children = [], childNames = []>
#packed = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false,
    range = [1, 0], children = [#bits], childNames = []>
#enum = #simulation.vpi_type<kind = enum, isSigned = false, isFourState = false,
    name = "choice_t", range = [], children = [#packed], childNames = []>
#logic = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
    range = [2, 0], children = [], childNames = []>
#wide = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
    range = [64, 0], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#real = #simulation.vpi_type<kind = real, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = true,
    name = "record_t", range = [], children = [#word, #enum, #string, #logic],
    childNames = ["count", "choice", "text", "flags"], isTagged = false, isSoft = false,
    bitWidth = 0 : i64, selectableWidth = 107 : i64, bitstreamWidth = 107 : i64,
    tagBits = 0 : i64, childOrdinals = [0, 1, 2, 3], childPackedOffsets = [0, 0, 0, 0]>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = true,
    range = [2, 1], children = [#record], childNames = []>
#queue = #simulation.vpi_type<kind = queue, isSigned = false, isFourState = false,
    range = [], children = [#record], childNames = [], queueBound = 0 : i64>
#assoc = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#packed, #record], childNames = [], wildcardIndex = false>
#aggregate_keys = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#record, #word], childNames = [], wildcardIndex = false>
#tagged = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "tagged_t", range = [], children = [#word, #string], childNames = ["count", "text"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 64 : i64,
    bitstreamWidth = 64 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#union = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = true,
    name = "union_t", range = [], children = [#wide, #real], childNames = ["bits", "number"],
    isTagged = false, isSoft = false, bitWidth = 0 : i64, selectableWidth = 65 : i64,
    bitstreamWidth = 65 : i64, tagBits = 0 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>

!record = !simulation.unpacked_struct<[
    #simulation.field<name = "count", type = i32, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "choice", type = i8, ordinal = 1, packedOffset = 0>,
    #simulation.field<name = "text", type = !simulation.string, ordinal = 2, packedOffset = 0>,
    #simulation.field<name = "flags", type = !simulation.logic<3>, ordinal = 3, packedOffset = 0>]>
!array = !simulation.unpacked_array<2 : 1 x !record>
!queue = !simulation.queue<!record, 0>
!assoc = !simulation.assoc_array<i8, !record, false, false>
!aggregate_keys = !simulation.assoc_array<!record, i32, false, false>
!tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "count", type = i32, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>], isTagged = true>
!union = !simulation.unpacked_union<fields = [
    #simulation.field<name = "bits", type = !simulation.logic<65>, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "number", type = f64, ordinal = 1, packedOffset = 0>], isTagged = false>

#array_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#array], childNames = []>

#queue_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#queue], childNames = []>

#assoc_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#assoc], childNames = []>

#aggregate_keys_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#aggregate_keys], childNames = []>

#tagged_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#tagged], childNames = []>

#union_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#union], childNames = []>

#enum_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#enum], childNames = []>

#nibble = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false,
    range = [3, 0], children = [], childNames = []>
#small_tagged = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "small_tagged_t", range = [], children = [#nibble, #nibble], childNames = ["first", "second"],
    isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 4 : i64,
    bitstreamWidth = 4 : i64, tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#small_tagged_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#small_tagged], childNames = []>
!small_tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "first", type = i4, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "second", type = i4, ordinal = 1, packedOffset = 0>], isTagged = true>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @mailbox_layouts {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    simulation.vpi_typespec.decl @array id 0 in 0 owner @std hierarchy "std::array$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #array_mailbox, message_type = !array}
    simulation.storage.decl 0 in 1 : !simulation.mailbox<!array> design hierarchy "top.array" {
      vpi_type = #array_mailbox}
    simulation.vpi_typespec.decl @queue id 1 in 0 owner @std hierarchy "std::queue$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #queue_mailbox, message_type = !queue}
    simulation.storage.decl 1 in 1 : !simulation.mailbox<!queue> design hierarchy "top.queue" {
      vpi_type = #queue_mailbox}
    simulation.vpi_typespec.decl @assoc id 2 in 0 owner @std hierarchy "std::assoc$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #assoc_mailbox, message_type = !assoc}
    simulation.storage.decl 2 in 1 : !simulation.mailbox<!assoc> design hierarchy "top.assoc" {
      vpi_type = #assoc_mailbox}
    simulation.vpi_typespec.decl @aggregate_keys id 3 in 0 owner @std hierarchy "std::aggregate_keys$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #aggregate_keys_mailbox, message_type = !aggregate_keys}
    simulation.storage.decl 3 in 1 : !simulation.mailbox<!aggregate_keys> design hierarchy "top.aggregate_keys" {
      vpi_type = #aggregate_keys_mailbox}
    simulation.vpi_typespec.decl @tagged id 4 in 0 owner @std hierarchy "std::tagged$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #tagged_mailbox, message_type = !tagged}
    simulation.storage.decl 4 in 1 : !simulation.mailbox<!tagged> design hierarchy "top.tagged" {
      vpi_type = #tagged_mailbox}
    simulation.vpi_typespec.decl @union id 5 in 0 owner @std hierarchy "std::union$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #union_mailbox, message_type = !union}
    simulation.storage.decl 5 in 1 : !simulation.mailbox<!union> design hierarchy "top.union" {
      vpi_type = #union_mailbox}
    simulation.vpi_typespec.decl @enum id 6 in 0 owner @std hierarchy "std::enum$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #enum_mailbox, message_type = i8}
    simulation.storage.decl 6 in 1 : !simulation.mailbox<i8> design hierarchy "top.enum" {
      vpi_type = #enum_mailbox}
    simulation.vpi_typespec.decl @small_tagged id 7 in 0 owner @std hierarchy "std::small_tagged$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #small_tagged_mailbox, message_type = !small_tagged}
    simulation.storage.decl 7 in 1 : !simulation.mailbox<!small_tagged> design hierarchy "top.small_tagged" {
      vpi_type = #small_tagged_mailbox}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
