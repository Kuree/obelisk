// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.MailboxWeakChildren

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#ref = #simulation.vpi_type<kind = semaphore, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#void = #simulation.vpi_type<kind = void, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = false,
    name = "record_t", range = [], children = [#ref, #int],
    childNames = ["reference", "number"], isTagged = false, isSoft = false,
    bitWidth = 0 : i64, selectableWidth = 32 : i64, bitstreamWidth = 0 : i64,
    tagBits = 0 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#union = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    name = "union_t", range = [], children = [#record, #void],
    childNames = ["record", "none"], isTagged = true, isSoft = false,
    bitWidth = 0 : i64, selectableWidth = 32 : i64, bitstreamWidth = 0 : i64,
    tagBits = 2 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
!record = !simulation.unpacked_struct<[
    #simulation.field<name = "reference", type = !simulation.semaphore, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "number", type = i32, ordinal = 1, packedOffset = 0>]>
!union = !simulation.unpacked_union<fields = [
    #simulation.field<name = "record", type = !record, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "none", type = i1, ordinal = 1, packedOffset = 0>], isTagged = true>
#record_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#record], childNames = []>
#union_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#union], childNames = []>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @weak_children {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    simulation.vpi_typespec.decl @ref id 0 in 0 owner @std
        hierarchy "std::semaphore" debug "semaphore" {
      origin = 4 : i32, target_type = #ref}
    simulation.vpi_typespec.decl @records id 1 in 0 owner @std
        hierarchy "std::record$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #record_mailbox, message_type = !record}
    simulation.vpi_typespec.decl @unions id 2 in 0 owner @std
        hierarchy "std::union$mailbox" debug "mailbox" {
      origin = 4 : i32, target_type = #union_mailbox, message_type = !union}
    simulation.storage.decl 0 in 1 : !simulation.mailbox<!record> design hierarchy "top.records" {
      vpi_type = #record_mailbox}
    simulation.storage.decl 1 in 1 : !simulation.mailbox<!union> design hierarchy "top.unions" {
      vpi_type = #union_mailbox}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
