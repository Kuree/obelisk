// RUN: obelisk-opt %s | FileCheck %s

module attributes {
  obelisk_sim.test_bool_property = #obelisk_sim.vpi_property<
    selector = 74 : i32, value = true>,
  obelisk_sim.test_i32_property = #obelisk_sim.vpi_property<
    selector = 4 : i32, value = 8 : i32>,
  obelisk_sim.test_i64_property = #obelisk_sim.vpi_property<
    selector = 660 : i32, value = 42 : i64>,
  obelisk_sim.test_string_property = #obelisk_sim.vpi_property<
    selector = 2 : i32, value = "top">
} {
  obelisk_sim.design @property_inventory {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 74 : i32, value = true>
      ]>,
      definition_loc = loc("definition.sv":3:1)
    } loc("use.sv":19:7)
  }
}

// CHECK: #loc = loc("definition.sv":3:1)
// CHECK: obelisk_sim.test_bool_property = #obelisk_sim.vpi_property<selector = 74 : i32, value = true>
// CHECK: obelisk_sim.test_i32_property = #obelisk_sim.vpi_property<selector = 4 : i32, value = 8 : i32>
// CHECK: obelisk_sim.test_i64_property = #obelisk_sim.vpi_property<selector = 660 : i32, value = 42 : i64>
// CHECK: obelisk_sim.test_string_property = #obelisk_sim.vpi_property<selector = 2 : i32, value = "top">
// CHECK: obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
// CHECK-SAME: definition_loc = #loc
// CHECK-SAME: vpi_properties = #obelisk_sim.vpi_properties<[
// CHECK-SAME: #obelisk_sim.vpi_property<selector = 74 : i32, value = true>
