// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.MailboxMessages

#wide_leaf = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
    range = [0, 0], children = [], childNames = []>
#wide_inner = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = true,
    range = [64, 0], children = [#wide_leaf], childNames = []>
#wide = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = true,
    range = [1, 0], children = [#wide_inner], childNames = []>
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#real = #simulation.vpi_type<kind = real, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#short_real = #simulation.vpi_type<kind = shortreal, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#class = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @C, range = [], children = [], childNames = []>
#wide_base = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @WideBase, range = [], children = [], childNames = []>
#wide_derived = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @WideDerived, range = [], children = [], childNames = []>
#semaphore = #simulation.vpi_type<kind = semaphore, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#word_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#word], childNames = []>
#wide_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#wide], childNames = []>
#real_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#real], childNames = []>
#short_real_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#short_real], childNames = []>
#string_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#string], childNames = []>
#class_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#class], childNames = []>
#semaphore_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#semaphore], childNames = []>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @mailbox_messages {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @WideBase id 2 {builtin_base = @wide_payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @WideDerived id 3 extends @WideBase {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @wide_payload of @WideBase at 0 : !simulation.mailbox<!simulation.packed_array<1 : 0 x !simulation.packed_array<64 : 0 x !simulation.logic<1>>>> {
      is_static = false, is_weak = false}
    simulation.vpi_object.anchor @WideBase_definition id 2 type 652 in 1 parent @top ordinal 1
        hierarchy "top.WideBase" debug "WideBase" {
      backing = #simulation.vpi_backing<kind = class, symbol = @WideBase>}
    simulation.vpi_object.anchor @WideDerived_definition id 3 type 652 in 1 parent @top ordinal 2
        hierarchy "top.WideDerived" debug "WideDerived" {
      backing = #simulation.vpi_backing<kind = class, symbol = @WideDerived>}
    simulation.vpi_typespec.decl @wide_base_type id 8 in 1 owner @WideBase_definition hierarchy "top.WideBase$type" debug "WideBase" {
      origin = 3 : i32, target_type = #wide_base}
    simulation.vpi_typespec.decl @wide_derived_type id 9 in 1 owner @WideDerived_definition hierarchy "top.WideDerived$type" debug "WideDerived" {
      origin = 3 : i32, target_type = #wide_derived}
    simulation.vpi_relation.decl <kind = symbol, symbol = @wide_base_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @wide_type>
    simulation.vpi_relation.decl <kind = symbol, symbol = @wide_derived_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @wide_base_type>
    simulation.vpi_object.anchor @C_definition id 1 type 652 in 1 parent @top ordinal 0
        hierarchy "top.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @C>}
    simulation.vpi_typespec.decl @C_type id 0 in 1 owner @C_definition hierarchy "top.C$type" debug "C" {
      origin = 3 : i32, target_type = #class}
    simulation.vpi_typespec.decl @wide_type id 1 in 1 owner @top hierarchy "top.wide$type" debug "wide_mailbox" {
      origin = 4 : i32, target_type = #wide_mailbox}
    simulation.vpi_typespec.decl @real_type id 2 in 1 owner @top hierarchy "top.real$type" debug "real_mailbox" {
      origin = 4 : i32, target_type = #real_mailbox}
    simulation.vpi_typespec.decl @string_type id 3 in 1 owner @top hierarchy "top.string$type" debug "string_mailbox" {
      origin = 4 : i32, target_type = #string_mailbox}
    simulation.vpi_typespec.decl @class_type id 4 in 1 owner @top hierarchy "top.class$type" debug "class_mailbox" {
      origin = 4 : i32, target_type = #class_mailbox}
    simulation.vpi_typespec.decl @semaphore_type id 5 in 1 owner @top hierarchy "top.semaphore$type" debug "semaphore_mailbox" {
      origin = 4 : i32, target_type = #semaphore_mailbox}
    simulation.vpi_typespec.decl @short_real_type id 6 in 1 owner @top hierarchy "top.short_real$type" debug "short_real_mailbox" {
      origin = 4 : i32, target_type = #short_real_mailbox}
    simulation.vpi_typespec.decl @word_type id 7 in 1 owner @top hierarchy "top.word$type" debug "word_mailbox" {
      origin = 4 : i32, target_type = #word_mailbox}
    simulation.storage.decl 0 in 1 : !simulation.mailbox<!simulation.packed_array<1 : 0 x !simulation.packed_array<64 : 0 x !simulation.logic<1>>>> design
        hierarchy "top.wide" {vpi_type = #wide_mailbox}
    simulation.storage.decl 1 in 1 : !simulation.mailbox<f64> design
        hierarchy "top.real" {vpi_type = #real_mailbox}
    simulation.storage.decl 2 in 1 : !simulation.mailbox<!simulation.string> design
        hierarchy "top.string" {vpi_type = #string_mailbox}
    simulation.storage.decl 3 in 1 : !simulation.mailbox<!simulation.class_handle<@C>> design
        hierarchy "top.object" {vpi_type = #class_mailbox}
    simulation.storage.decl 4 in 1 : !simulation.mailbox<!simulation.semaphore> design
        hierarchy "top.semaphore" {vpi_type = #semaphore_mailbox}
    simulation.storage.decl 5 in 1 : !simulation.mailbox<f32> design
        hierarchy "top.short_real" {vpi_type = #short_real_mailbox}
    simulation.storage.decl 6 in 1 : !simulation.mailbox<i32> design
        hierarchy "top.word" {vpi_type = #word_mailbox}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
