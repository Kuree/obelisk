// RUN: obelisk-opt %s | FileCheck %s
// IEEE 1800-2023 37.43: a frame identifies the actual invocation. Nominal
// interface membership and declaration-owned extern providers prove targets
// independently of the executable dispatch branch list.
#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
#other = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus#(2)", symbol = @other, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 1 hierarchy "top.provider" vpi_kind 32
    simulation.scope.decl 4 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 5 parent 4 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.vpi_typespec.decl @other id 1 in 1 owner @owner hierarchy "bus#(2)" debug "bus#(2)" {origin = 1 : i32, target_type = #other}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 47 in 2 function hierarchy "top.bus.g" debug "g"
    simulation.code_unit.decl 48 in 3 function hierarchy "top.provider.g" debug "g"
    simulation.code_unit.decl 54 in 5 function hierarchy "second.bus.f" debug "f"
    simulation.code_unit.decl 57 in 5 function hierarchy "second.bus.g" debug "g"
    simulation.code_unit.decl 60 in 0 function hierarchy "$unit.invoke" debug "invoke"
    simulation.code_unit.decl 64 in 4 initial hierarchy "second.initial"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_member.decl @g of @bus name "g" function signature (i32) -> i32 directions [3] {is_extern}
    simulation.vpi_interface_member.decl @empty of @other name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 5 to @bus
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 54 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 57 signature (i32) -> i32 directions [3] member @g {interface_exports = array<i64: 48>}
    simulation.vpi_callable.decl 47 signature (i32) -> i32 directions [3] member @g {interface_exports = array<i64: 48>}
    simulation.vpi_callable.decl 48 signature (i32) -> i32 directions [3]
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @export id 11 in 44 scope 1 type 19 name "g" {is_indirect}
    simulation.vpi_call.decl @no_target id 12 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @global id 13 in 60 scope 0 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @second id 14 in 64 scope 4 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
    simulation.vpi_interface_call.dispatch @export member @g access 2
    simulation.vpi_interface_call.dispatch @no_target member @empty access 0
    simulation.vpi_interface_call.dispatch @global member @f access 0
    simulation.vpi_interface_call.dispatch @second member @f access 1
  }
}
// CHECK: simulation.vpi_callable.decl 47 signature (i32) -> i32 directions [3] member @g {interface_exports = array<i64: 48>}
// CHECK: simulation.vpi_interface_call.dispatch @call member @f access 0
// CHECK: simulation.vpi_interface_call.dispatch @export member @g access 2
// CHECK: simulation.vpi_interface_call.dispatch @no_target member @empty access 0
// CHECK: simulation.vpi_interface_call.dispatch @global member @f access 0
// CHECK: simulation.vpi_interface_call.dispatch @second member @f access 1
