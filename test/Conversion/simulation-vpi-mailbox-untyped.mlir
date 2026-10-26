// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.UntypedMailboxMessages

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#alias = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = [], typedefAliases = [@word_alias]>
#wide = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
    range = [129, 0], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#class = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @C, range = [], children = [], childNames = []>
#queue = #simulation.vpi_type<kind = queue, isSigned = false, isFourState = false,
    range = [], children = [#word], childNames = [], queueBound = 0 : i64>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = false,
    range = [], children = [#word, #string, #queue, #class],
    childNames = ["number", "text", "queue", "object"], isTagged = false, isSoft = false,
    bitWidth = 0 : i64, selectableWidth = 160 : i64, bitstreamWidth = 0 : i64,
    tagBits = 0 : i64, childOrdinals = [0, 1, 2, 3], childPackedOffsets = [0, 0, 0, 0]>
!record = !simulation.unpacked_struct<[
    #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
    #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>,
    #simulation.field<name = "queue", type = !simulation.queue<i32, 0>, ordinal = 2, packedOffset = 0>,
    #simulation.field<name = "object", type = !simulation.class_handle<@C>, ordinal = 3, packedOffset = 0>]>
#assoc = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
    range = [], children = [#word, #word], childNames = [], wildcardIndex = false>
!assoc = !simulation.assoc_array<i32, i32, true, false>
#untyped = #simulation.vpi_type<kind = untyped, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#untyped], childNames = []>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @untyped_messages {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.class.decl @C id 1 {is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @C_definition id 1 type 652 in 1 parent @top ordinal 0
        hierarchy "top.C" debug "C" {backing = #simulation.vpi_backing<kind = class, symbol = @C>}
    simulation.vpi_typespec.decl @C_type id 0 in 1 owner @C_definition
        hierarchy "top.C$type" debug "C" {origin = 3 : i32, target_type = #class}
    simulation.vpi_typespec.decl @mailbox id 1 in 1 owner @top
        hierarchy "top.mailbox$type" debug "mailbox" {
      origin = 4 : i32, target_type = #mailbox, message_type = !simulation.box}
    simulation.vpi_typespec.decl @word_alias id 2 in 1 owner @top
        hierarchy "top.Word" debug "Word" {target_type = #word}
    simulation.storage.decl 0 in 1 : !simulation.mailbox<!simulation.box> design
        hierarchy "top.messages" {vpi_type = #mailbox}
    simulation.vpi_box_type.decl @signed_word id 1 : i32 {vpi_type = #word}
    simulation.vpi_box_type.decl @unsigned_word id 2 : i32 {vpi_type = #bits}
    simulation.vpi_box_type.decl @aliased_word id 3 : i32 {vpi_type = #alias}
    simulation.vpi_box_type.decl @wide id 4 : !simulation.logic<130> {vpi_type = #wide}
    simulation.vpi_box_type.decl @text id 5 : !simulation.string {vpi_type = #string}
    simulation.vpi_box_type.decl @record id 6 : !record {vpi_type = #record}
    simulation.vpi_box_type.decl @sparse id 7 : !assoc {vpi_type = #assoc}
    simulation.vpi_box_type.decl @object id 8 : !simulation.class_handle<@C> {vpi_type = #class}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
