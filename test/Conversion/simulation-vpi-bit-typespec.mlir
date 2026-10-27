// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.BitTypespec

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#integer = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true,
    range = [31, 0], children = [], childNames = []>
#logic = #simulation.vpi_type<kind = logic, isSigned = true, isFourState = true,
    range = [7, 0], children = [], childNames = [], typedefAliases = [@word]>
#enum = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false,
    name = "state_t", range = [], children = [#int], childNames = [], typedefAliases = [@state]>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @bit_typespec {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @word id 0 in 1 owner @top
        hierarchy "top.word_t" debug "word_t" {target_type = #logic}
    simulation.vpi_typespec.decl @state id 1 in 1 owner @top
        hierarchy "top.state_t" debug "state_t" {target_type = #enum}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.signed_int" {
      vpi_type = #int}
    simulation.storage.decl 1 in 1 : !simulation.logic<32> design hierarchy "top.integer" {
      vpi_type = #integer}
    simulation.storage.decl 2 in 1 : !simulation.logic<8> design hierarchy "top.word" {
      vpi_type = #logic}
    simulation.storage.decl 3 in 1 : i32 design hierarchy "top.state" {
      vpi_type = #enum}
    simulation.net.decl 0 in 1 : i32 design hierarchy "top.int_net" {
      vpi_type = #int}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
