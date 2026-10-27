// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.MailboxAssociativeMessages

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#wide_key = #simulation.vpi_type<kind = logic, isSigned = true, isFourState = true,
    range = [129, 0], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = false,
    name = "key_t", range = [], children = [#word, #string], childNames = ["number", "text"],
    isTagged = false, isSoft = false, bitWidth = 0 : i64,
    selectableWidth = 96 : i64, bitstreamWidth = 96 : i64, tagBits = 0 : i64,
    childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#untyped = #simulation.vpi_type<kind = untyped, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#fixed = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false,
    range = [2, 1], children = [#word], childNames = []>
!record = !simulation.unpacked_struct<[
    #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>]>
!fixed = !simulation.unpacked_array<2 : 1 x i32>
#wide = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#wide_key, #word], childNames = [], wildcardIndex = false>
#wide_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#wide], childNames = []>
!wide = !simulation.assoc_array<!simulation.logic<130>, i32, true, false>
#strings = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#string, #word], childNames = [], wildcardIndex = false>
#strings_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#strings], childNames = []>
!strings = !simulation.assoc_array<!simulation.string, i32, false, false>
#records = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#record, #word], childNames = [], wildcardIndex = false>
#records_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#records], childNames = []>
!records = !simulation.assoc_array<!record, i32, false, false>
#wildcard = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#untyped, #word], childNames = [], wildcardIndex = true>
#wildcard_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#wildcard], childNames = []>
!wildcard = !simulation.assoc_array<!simulation.box, i32, false, true>
#nested = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#word, #fixed], childNames = [], wildcardIndex = false>
#nested_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#nested], childNames = []>
!nested = !simulation.assoc_array<i32, !fixed, true, false>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @associative_messages {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    simulation.vpi_typespec.decl @wide id 0 in 0 owner @std hierarchy "std::wide$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #wide_mailbox, message_type = !wide}
    simulation.storage.decl 0 in 1 : !simulation.mailbox<!wide> design hierarchy "top.wide" {vpi_type = #wide_mailbox}
    simulation.vpi_typespec.decl @strings id 1 in 0 owner @std hierarchy "std::strings$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #strings_mailbox, message_type = !strings}
    simulation.storage.decl 1 in 1 : !simulation.mailbox<!strings> design hierarchy "top.strings" {vpi_type = #strings_mailbox}
    simulation.vpi_typespec.decl @records id 2 in 0 owner @std hierarchy "std::records$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #records_mailbox, message_type = !records}
    simulation.storage.decl 2 in 1 : !simulation.mailbox<!records> design hierarchy "top.records" {vpi_type = #records_mailbox}
    simulation.vpi_typespec.decl @wildcard id 3 in 0 owner @std hierarchy "std::wildcard$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #wildcard_mailbox, message_type = !wildcard}
    simulation.storage.decl 3 in 1 : !simulation.mailbox<!wildcard> design hierarchy "top.wildcard" {vpi_type = #wildcard_mailbox}
    simulation.vpi_typespec.decl @nested id 4 in 0 owner @std hierarchy "std::nested$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #nested_mailbox, message_type = !nested}
    simulation.storage.decl 4 in 1 : !simulation.mailbox<!nested> design hierarchy "top.nested" {vpi_type = #nested_mailbox}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
