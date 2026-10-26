// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{terminal must name a primitive terminal in this design}}
    simulation.vpi_terminal_driver.bind @gate low 0 high 1 bit 0
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 1 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    // expected-error @+1 {{terminal must retain output direction}}
    simulation.vpi_terminal_driver.bind @term low 0 high 1 bit 0
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{driver bit offset exceeds the 32-bit reflection field}}
    simulation.vpi_terminal_driver.bind @term low 0 high 1 bit 4294967296
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{driver bank count must match the primitive subtype}}
    simulation.vpi_terminal_driver.bind @term low 0 bit 0
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "buf" primitive_type 7
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{driver bank count must match the primitive subtype}}
    simulation.vpi_terminal_driver.bind @term low 0 high 1 bit 0
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{drivers must contain the selected bit}}
    simulation.vpi_terminal_driver.bind @term low 0 high 1 bit 1
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{drivers must contain the selected bit}}
    simulation.vpi_terminal_driver.bind @term low 0 high 0 bit 0
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 6 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @gate to @kind
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{drivers must be one exact low/high polarity pair}}
    simulation.vpi_terminal_driver.bind @term low 0 high 1 bit 0
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 6 : i32, strength1 = 0 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 0 : i32}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design {driven_low = 0 : i64, driven_width = 1 : i64, strength0 = 0 : i32, strength1 = 6 : i32, simulation.strength_group = 5 : i64, simulation.strength_bank = 1 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @kind type 21 name "bufif1" primitive_type 10
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{requires the exact retained gate primitive type}}
    simulation.vpi_terminal_driver.bind @term low 0 high 1 bit 0
  }
}
