// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.LiveBuiltinClasses

#sem = #simulation.vpi_type<kind = semaphore, isSigned = false,
    isFourState = false, range = [], children = [], childNames = []>
#process = #simulation.vpi_type<kind = process, isSigned = false,
    isFourState = false, range = [], children = [], childNames = []>
#aliased_process = #simulation.vpi_type<kind = process, isSigned = false,
    isFourState = false, range = [], children = [], childNames = [],
    typedefAliases = [@process_alias]>
#word = #simulation.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31, 0], children = [], childNames = []>
#longword = #simulation.vpi_type<kind = longint, isSigned = true,
    isFourState = false, range = [63, 0], children = [], childNames = []>
#word_alias = #simulation.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31, 0], children = [], childNames = [],
    typedefAliases = [@word_alias]>
#mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [], children = [#word], childNames = []>
#aliased_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [], children = [#word_alias], childNames = []>
#wide_mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [], children = [#longword], childNames = []>
#S = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @S, range = [], children = [], childNames = []>
#T = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @T, range = [], children = [], childNames = []>
#M = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @M, range = [], children = [], childNames = []>
#one = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @one, range = [], children = [], childNames = []>
#two = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
    symbol = @two, range = [], children = [], childNames = []>
#mailbox_one = #simulation.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [], children = [#one], childNames = []>
#mailbox_two = #simulation.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [], children = [#two], childNames = []>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @live_builtins {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.class.decl @S id 1 {builtin_base = @semaphore_payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @T id 2 extends @S {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @M id 3 {builtin_base = @mailbox_payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @semaphore_payload of @S at 0 : !simulation.semaphore {
      is_static = false, is_weak = false}
    simulation.class.field @mailbox_payload of @M at 0 : !simulation.mailbox<i32> {
      is_static = false, is_weak = false}
    simulation.class.decl @one id 4 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @two id 5 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @S_definition id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.S" debug "S" {
      backing = #simulation.vpi_backing<kind = class, symbol = @S>}
    simulation.vpi_object.anchor @T_definition id 2 type 652 in 1 parent @top
        ordinal 1 hierarchy "top.T" debug "T" {
      backing = #simulation.vpi_backing<kind = class, symbol = @T>}
    simulation.vpi_object.anchor @M_definition id 3 type 652 in 1 parent @top
        ordinal 2 hierarchy "top.M" debug "M" {
      backing = #simulation.vpi_backing<kind = class, symbol = @M>}
    simulation.vpi_object.anchor @generic id 4 type 652 in 1 parent @top
        ordinal 3 hierarchy "top.Generic" debug "Generic"
    simulation.vpi_typespec.decl @S_type id 0 in 1 owner @S_definition
        hierarchy "top.S$type" debug "S" {origin = 3 : i32, target_type = #S}
    simulation.vpi_typespec.decl @T_type id 1 in 1 owner @T_definition
        hierarchy "top.T$type" debug "T" {origin = 3 : i32, target_type = #T}
    simulation.vpi_typespec.decl @M_type id 2 in 1 owner @M_definition
        hierarchy "top.M$type" debug "M" {origin = 3 : i32, target_type = #M}
    simulation.vpi_typespec.decl @semaphore id 3 in 1 owner @top
        hierarchy "top.semaphore$type" debug "semaphore" {
      origin = 4 : i32, target_type = #sem}
    simulation.vpi_typespec.decl @mailbox id 4 in 1 owner @top
        hierarchy "top.mailbox$type" debug "mailbox_int" {
      origin = 4 : i32, target_type = #mailbox}
    simulation.vpi_typespec.decl @word_alias id 5 in 1 owner @top
        hierarchy "top.Word" debug "Word" {target_type = #word}
    simulation.vpi_typespec.decl @wide_mailbox id 6 in 1 owner @top
        hierarchy "top.wide_mailbox$type" debug "mailbox_longint" {
      origin = 4 : i32, target_type = #wide_mailbox}
    simulation.vpi_typespec.decl @one_type id 7 in 1 owner @generic
        hierarchy "top.Generic$one" debug "Generic" {
      origin = 3 : i32, target_type = #one}
    simulation.vpi_typespec.decl @two_type id 8 in 1 owner @generic
        hierarchy "top.Generic$two" debug "Generic" {
      origin = 3 : i32, target_type = #two}
    simulation.vpi_typespec.decl @mailbox_one id 9 in 1 owner @top
        hierarchy "top.mailbox_one$type" debug "mailbox_one" {
      origin = 4 : i32, target_type = #mailbox_one}
    simulation.vpi_typespec.decl @mailbox_two id 10 in 1 owner @top
        hierarchy "top.mailbox_two$type" debug "mailbox_two" {
      origin = 4 : i32, target_type = #mailbox_two}
    simulation.vpi_typespec.decl @mailbox_alias id 11 in 1 owner @top
        hierarchy "top.MailboxAlias" debug "MailboxAlias" {target_type = #mailbox}
    simulation.vpi_typespec.decl @process id 12 in 1 owner @top
        hierarchy "top.process$type" debug "process" {
      origin = 4 : i32, target_type = #process}
    simulation.vpi_typespec.decl @process_alias id 13 in 1 owner @top
        hierarchy "top.ProcessAlias" debug "ProcessAlias" {target_type = #process}
    simulation.storage.decl 0 in 1 : !simulation.semaphore design
        hierarchy "top.semaphore" {vpi_type = #sem}
    simulation.storage.decl 1 in 1 : !simulation.mailbox<i32> design
        hierarchy "top.aliased" {vpi_type = #aliased_mailbox}
    simulation.storage.decl 2 in 1 : !simulation.mailbox<i32> design
        hierarchy "top.mailbox" {vpi_type = #mailbox}
    simulation.storage.decl 3 in 1 : !simulation.mailbox<i64> design
        hierarchy "top.wide_mailbox" {vpi_type = #wide_mailbox}
    simulation.storage.decl 4 in 1 : !simulation.mailbox<!simulation.class_handle<@one>> design
        hierarchy "top.mailbox_one" {vpi_type = #mailbox_one}
    simulation.storage.decl 5 in 1 : !simulation.mailbox<!simulation.class_handle<@two>> design
        hierarchy "top.mailbox_two" {vpi_type = #mailbox_two}
    simulation.storage.decl 6 in 1 : !simulation.process design
        hierarchy "top.process" {vpi_type = #process}
    simulation.storage.decl 7 in 1 : !simulation.process design
        hierarchy "top.process_alias" {vpi_type = #aliased_process}
    simulation.vpi_relation.decl <kind = symbol, symbol = @S_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @semaphore>
    simulation.vpi_relation.decl <kind = symbol, symbol = @T_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @S_type>
    simulation.vpi_relation.decl <kind = symbol, symbol = @M_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @mailbox>
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
