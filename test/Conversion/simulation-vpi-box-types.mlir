// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.BoxTypeLayouts
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#words = #simulation.vpi_type<kind = dynamic_array, isSigned = false, isFourState = false, range = [], children = [#word], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [31, 0], children = [], childNames = []>
#enum = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false, range = [], children = [#word], childNames = [], name = "choice_t">
#choice = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false, range = [], children = [#word], childNames = [], name = "choice_t", typedefAliases = [@choice]>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @boxes {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @unit id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit"
    simulation.vpi_typespec.decl @choice id 0 in 0 owner @unit hierarchy "$unit::choice_t" debug "choice_t" {origin = 0 : i32, target_type = #enum}
    simulation.vpi_box_type.decl @signed id 7 : i32 {vpi_type = #word}
    simulation.vpi_box_type.decl @unsigned id 8 : i32 {vpi_type = #bits}
    simulation.vpi_box_type.decl @enum id 9 : i32 {vpi_type = #choice}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #word}
    simulation.storage.decl 1 in 1 : !simulation.dynamic_array<i32> design hierarchy "top.box" {vpi_type = #words}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %output = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.dynamic_array<i32>>
      %one = arith.constant 1 : i64
      %zero = arith.constant 0 : i64
      %value = arith.constant -42 : i32
      %box = simulation.container.create %one {
        type_id = 1 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 4 : i64,
        alignment = 4 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i32>
      simulation.container.write %box, %zero, %value :
        (!simulation.dynamic_array<i32>, i64, i32) -> ()
      simulation.box.bind_type %box to @signed : !simulation.dynamic_array<i32>
      simulation.ref.store %box to %output : !simulation.dynamic_array<i32>,
        !simulation.ref<!simulation.dynamic_array<i32>>
      simulation.return
    }
  }
}
