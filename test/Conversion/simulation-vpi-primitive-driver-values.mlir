// RUN: obelisk-opt %s -o /dev/null
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.PrimitiveDriverValues
// LLVM-NOT: simulation.vpi_definition.bind
// LLVM-NOT: simulation.vpi_terminal_driver.bind
// LLVM: llvm.func
// LLVM-NOT: simulation.vpi_definition.bind
// LLVM-NOT: simulation.vpi_terminal_driver.bind
#logic = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
#wide = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [64, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @primitive_values {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" debug "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @buf type 21 name "buf" primitive_type 7
    simulation.vpi_definition.decl @bufif type 21 name "bufif1" primitive_type 10
    simulation.net.decl 0 in 1 : !simulation.logic<1> design hierarchy "top.out" {vpi_type = #logic}
    simulation.net.decl 1 in 1 : !simulation.logic<1> design hierarchy "top.input" {vpi_type = #logic}
    simulation.net.decl 2 in 1 : !simulation.logic<1> design hierarchy "top.conditional_net" {vpi_type = #logic}
    simulation.net.decl 3 in 1 : !simulation.logic<65> design hierarchy "top.wide_net" {vpi_type = #wide}
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 6 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 6 : i32}
    simulation.driver.decl 2 in 1 drives 2 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 3 in 1 drives 2 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.driver.decl 4 in 1 drives 2 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 6 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 5 in 1 drives 2 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 6 : i64, simulation.strength_bank = 1 : i32}
    simulation.driver.decl 6 in 1 drives 3 : !simulation.logic<65> design {driven_low = 64 : i64, driven_width = 1 : i64, strength0 = 3 : i32, strength1 = 3 : i32}
    simulation.driver.decl 7 in 1 drives 1 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 6 : i32}
    simulation.driver.decl 8 in 1 drives 3 : !simulation.logic<65> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 5 : i32, strength1 = 5 : i32}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.index" {vpi_type = #index}
    simulation.vpi_object.anchor @ordinary id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.ordinary" debug "ordinary" {primitive_input_count = 1 : i64}
    simulation.vpi_definition.bind @ordinary to @buf
    simulation.vpi_object.anchor @ordinary_0 id 2 type 46 in 1 parent @ordinary ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_terminal_driver.bind @ordinary_0 low 0 bit 0
    simulation.vpi_object.anchor @ordinary_1 id 3 type 46 in 1 parent @ordinary ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @ordinary_1> selector 102 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_object.anchor @conditional id 4 type 21 in 1 parent @top ordinal 1 hierarchy "top.conditional" debug "conditional" {primitive_input_count = 2 : i64}
    simulation.vpi_definition.bind @conditional to @bufif
    simulation.vpi_object.anchor @conditional_0 id 5 type 46 in 1 parent @conditional ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_terminal_driver.bind @conditional_0 low 2 high 3 bit 0
    simulation.vpi_object.anchor @conditional_1 id 6 type 46 in 1 parent @conditional ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @conditional_1> selector 102 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_object.anchor @conditional_2 id 7 type 46 in 1 parent @conditional ordinal 2 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @conditional_2> selector 102 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_object.anchor @wide id 8 type 21 in 1 parent @top ordinal 2 hierarchy "top.wide" debug "wide" {primitive_input_count = 1 : i64}
    simulation.vpi_definition.bind @wide to @buf
    simulation.vpi_object.anchor @wide_0 id 9 type 46 in 1 parent @wide ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_terminal_driver.bind @wide_0 low 6 bit 64
    simulation.vpi_object.anchor @wide_1 id 10 type 46 in 1 parent @wide ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_object.anchor @multi id 11 type 21 in 1 parent @top ordinal 3 hierarchy "top.multi" debug "multi" {primitive_input_count = 1 : i64}
    simulation.vpi_definition.bind @multi to @buf
    simulation.vpi_object.anchor @multi_0 id 12 type 46 in 1 parent @multi ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_terminal_driver.bind @multi_0 low 0 bit 0
    simulation.vpi_object.anchor @multi_1 id 13 type 46 in 1 parent @multi ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_terminal_driver.bind @multi_1 low 1 bit 0
    simulation.vpi_object.anchor @multi_2 id 14 type 46 in 1 parent @multi ordinal 2 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @multi_2> selector 102 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_object.anchor @unbound id 15 type 21 in 1 parent @top ordinal 4 hierarchy "top.unbound" debug "unbound" {primitive_input_count = 1 : i64}
    simulation.vpi_definition.bind @unbound to @buf
    simulation.vpi_object.anchor @unbound_0 id 16 type 46 in 1 parent @unbound ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_object.anchor @unbound_1 id 17 type 46 in 1 parent @unbound ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_expression.decl @selected id 100 type 42 in 1 owned_by <kind = symbol, symbol = @wrapped> selector 97 iterate ordinal 0 : !simulation.logic<1> {vpi_type = #logic}
    simulation.vpi_expression.decl @wrapped id 105 type 39 in 1 owned_by <kind = symbol, symbol = @wide_1> selector 102 handle ordinal 0 : !simulation.logic<1> {vpi_type = #logic, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @wide_1> selector 102 handle ordinal 0 to <kind = symbol, symbol = @wrapped>
    simulation.vpi_relation.decl <kind = symbol, symbol = @wrapped> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @selected>
    simulation.vpi_relation.decl <kind = symbol, symbol = @selected> selector 81 handle ordinal 0 to <kind = net, id = 3 : i64>
    simulation.vpi_expression.decl @bound0 id 101 type 7 in 1 owned_by <kind = symbol, symbol = @selected> selector 79 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [64 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @selected> selector 79 handle ordinal 0 to <kind = symbol, symbol = @bound0>
    simulation.vpi_expression.decl @bound1 id 102 type 7 in 1 owned_by <kind = symbol, symbol = @selected> selector 83 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [64 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @selected> selector 83 handle ordinal 0 to <kind = symbol, symbol = @bound1>
    simulation.vpi_expression.decl @indexed id 103 type 130 in 1 owned_by <kind = symbol, symbol = @unbound_1> selector 102 handle ordinal 0 : !simulation.logic<1> {vpi_type = #logic, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @unbound_1> selector 102 handle ordinal 0 to <kind = symbol, symbol = @indexed>
    simulation.vpi_relation.decl <kind = symbol, symbol = @indexed> selector 81 handle ordinal 0 to <kind = net, id = 3 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @indexed> selector 131 handle ordinal 0 to <kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @select_width id 104 type 7 in 1 owned_by <kind = symbol, symbol = @indexed> selector 132 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @indexed> selector 132 handle ordinal 0 to <kind = symbol, symbol = @select_width>
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.phase0"
    simulation.func @phase0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %v0 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d0 = %v0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %v1 = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d1 = %v1 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %v2 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d2 = %v2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %v3 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d3 = %v3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %v4 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d4 = %v4 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d5 = simulation.context.driver %ctx[5] : !simulation.driver<!simulation.logic<1>>
      %v5 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d5 = %v5 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d6 = simulation.context.driver %ctx[6] : !simulation.driver<!simulation.logic<65>>
      %v6 = simulation.logic.constant 18446744073709551616 : i65, 0 : i65 : !simulation.logic<65>
      simulation.driver.drive %d6 = %v6 : !simulation.driver<!simulation.logic<65>>, !simulation.logic<65>
      %d7 = simulation.context.driver %ctx[7] : !simulation.driver<!simulation.logic<1>>
      %v7 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d7 = %v7 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d8 = simulation.context.driver %ctx[8] : !simulation.driver<!simulation.logic<65>>
      %v8 = simulation.logic.constant 0 : i65, 0 : i65 : !simulation.logic<65>
      simulation.driver.drive %d8 = %v8 : !simulation.driver<!simulation.logic<65>>, !simulation.logic<65>
      %index_ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %index_value = arith.constant 64 : i32
      simulation.ref.store %index_value to %index_ref : i32, !simulation.ref<i32>
      simulation.return
    }
    simulation.code_unit.decl 45 in 1 initial hierarchy "top.phase1"
    simulation.func @phase1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} {
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %v0 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d0 = %v0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %v1 = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d1 = %v1 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %v2 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d2 = %v2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %v3 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d3 = %v3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %v4 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d4 = %v4 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d5 = simulation.context.driver %ctx[5] : !simulation.driver<!simulation.logic<1>>
      %v5 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d5 = %v5 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d6 = simulation.context.driver %ctx[6] : !simulation.driver<!simulation.logic<65>>
      %v6 = simulation.logic.constant 18446744073709551616 : i65, 0 : i65 : !simulation.logic<65>
      simulation.driver.drive %d6 = %v6 : !simulation.driver<!simulation.logic<65>>, !simulation.logic<65>
      %d7 = simulation.context.driver %ctx[7] : !simulation.driver<!simulation.logic<1>>
      %v7 = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d7 = %v7 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d8 = simulation.context.driver %ctx[8] : !simulation.driver<!simulation.logic<65>>
      %v8 = simulation.logic.constant 0 : i65, 0 : i65 : !simulation.logic<65>
      simulation.driver.drive %d8 = %v8 : !simulation.driver<!simulation.logic<65>>, !simulation.logic<65>
      %index_ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %index_value = arith.constant 0 : i32
      simulation.ref.store %index_value to %index_ref : i32, !simulation.ref<i32>
      simulation.return
    }
    simulation.code_unit.decl 46 in 1 initial hierarchy "top.phase2"
    simulation.func @phase2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 46 : i64} {
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %v0 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d0 = %v0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %v1 = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d1 = %v1 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %v2 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d2 = %v2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %v3 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d3 = %v3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %v4 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d4 = %v4 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d5 = simulation.context.driver %ctx[5] : !simulation.driver<!simulation.logic<1>>
      %v5 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.driver.drive %d5 = %v5 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d6 = simulation.context.driver %ctx[6] : !simulation.driver<!simulation.logic<65>>
      %v6 = simulation.logic.constant 18446744073709551616 : i65, 0 : i65 : !simulation.logic<65>
      simulation.driver.drive %d6 = %v6 : !simulation.driver<!simulation.logic<65>>, !simulation.logic<65>
      %d7 = simulation.context.driver %ctx[7] : !simulation.driver<!simulation.logic<1>>
      %v7 = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.driver.drive %d7 = %v7 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %d8 = simulation.context.driver %ctx[8] : !simulation.driver<!simulation.logic<65>>
      %v8 = simulation.logic.constant 0 : i65, 0 : i65 : !simulation.logic<65>
      simulation.driver.drive %d8 = %v8 : !simulation.driver<!simulation.logic<65>>, !simulation.logic<65>
      %index_ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %index_value = arith.constant 65 : i32
      simulation.ref.store %index_value to %index_ref : i32, !simulation.ref<i32>
      simulation.return
    }
  }
}
