// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.InterfaceDPITaskFrameTransport
// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.InterfaceCallDispatch
// IEEE 1800-2023 37.43: a frame identifies the actual invocation. Nominal
// interface membership and declaration-owned extern providers prove targets
// independently of the executable dispatch branch list.
#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
#other = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus#(2)", symbol = @other, modport = "", range = [], children = [], childNames = []>
#different = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "different", symbol = @different, modport = "", range = [], children = [], childNames = []>
#dpi = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "dpi", symbol = @dpi, modport = "", range = [], children = [], childNames = []>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 1 hierarchy "top.provider" vpi_kind 32
    simulation.scope.decl 4 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 5 parent 4 hierarchy "second.bus" vpi_kind 601
    simulation.scope.decl 6 parent 4 hierarchy "second.different" vpi_kind 601
    simulation.scope.decl 7 parent 4 hierarchy "second.dpi" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.vpi_typespec.decl @other id 1 in 1 owner @owner hierarchy "bus#(2)" debug "bus#(2)" {origin = 1 : i32, target_type = #other}
    simulation.vpi_typespec.decl @different id 2 in 1 owner @owner hierarchy "different" debug "different" {origin = 1 : i32, target_type = #different}
    simulation.vpi_typespec.decl @dpi id 3 in 1 owner @owner hierarchy "dpi" debug "dpi" {origin = 1 : i32, target_type = #dpi}
    simulation.vpi_object.anchor @dpi_scope id 1 type 601 in 7 ordinal 1 hierarchy "second.dpi" debug "dpi" {backing = #simulation.vpi_backing<kind = scope, id = 7 : i64>}
    simulation.vpi_object.anchor @dpi_f_source id 2 type 20 in 7 parent @dpi_scope ordinal 0 hierarchy "second.dpi.dpi_f" debug "dpi_f"
    simulation.vpi_object.anchor @dpi_t_source id 3 type 59 in 7 parent @dpi_scope ordinal 1 hierarchy "second.dpi.dpi_t" debug "dpi_t"
    simulation.code_unit.decl 100 in 7 function hierarchy "second.dpi.dpi_f" debug "dpi_f" {dpi.import}
    simulation.code_unit.decl 101 in 7 function hierarchy "second.dpi.dpi_t" debug "dpi_t" {dpi.import, dpi.task}
    simulation.vpi_code_unit_scope.bind 100 to @dpi_f_source
    simulation.vpi_code_unit_scope.bind 101 to @dpi_t_source
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 47 in 2 function hierarchy "top.bus.g" debug "g"
    simulation.code_unit.decl 48 in 3 function hierarchy "top.provider.g" debug "g"
    simulation.code_unit.decl 54 in 5 function hierarchy "second.bus.f" debug "f"
    simulation.code_unit.decl 57 in 5 function hierarchy "second.bus.g" debug "g"
    simulation.code_unit.decl 60 in 0 function hierarchy "$unit.invoke" debug "invoke"
    simulation.code_unit.decl 64 in 4 initial hierarchy "second.initial"
    simulation.code_unit.decl 70 in 2 task hierarchy "top.bus.h" debug "h"
    simulation.code_unit.decl 71 in 5 task hierarchy "second.bus.h" debug "h"
    simulation.code_unit.decl 72 in 3 task hierarchy "top.provider.first" debug "first"
    simulation.code_unit.decl 73 in 3 task hierarchy "top.provider.second" debug "second"
    simulation.code_unit.decl 74 in 2 task hierarchy "top.bus.j" debug "j"
    simulation.code_unit.decl 75 in 5 task hierarchy "second.bus.j" debug "j"
    simulation.code_unit.decl 76 in 3 task hierarchy "top.provider.j" debug "j"
    simulation.code_unit.decl 80 in 6 function hierarchy "second.different.f" debug "f"
    simulation.code_unit.decl 90 in 3 function hierarchy "top.provider.wrong_abi" debug "wrong_abi"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_member.decl @g of @bus name "g" function signature (i32) -> i32 directions [3] {is_extern}
    simulation.vpi_interface_member.decl @empty of @other name "f" function signature () -> i32 directions []
    simulation.vpi_interface_member.decl @h of @bus name "h" task signature () -> () directions [] {is_extern}
    simulation.vpi_interface_member.decl @j of @bus name "j" task signature () -> () directions [] {is_extern}
    simulation.vpi_interface_member.decl @different_f of @different name "f" function signature (i64) -> i32 directions [1] {is_extern}
    simulation.vpi_interface_member.decl @dpi_f of @dpi name "dpi_f" function signature () -> i32 directions []
    simulation.vpi_interface_member.decl @dpi_t of @dpi name "dpi_t" task signature () -> () directions []
    simulation.vpi_interface_instance.bind 7 to @dpi
    simulation.vpi_callable.decl 100 signature () -> i32 directions [] member @dpi_f
    simulation.vpi_callable.decl 101 signature () -> () directions [] member @dpi_t
    simulation.vpi_interface_instance.bind 6 to @different
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 5 to @bus
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 54 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 57 signature (i32) -> i32 directions [3] member @g {interface_exports = array<i64: 48>}
    simulation.vpi_callable.decl 47 signature (i32) -> i32 directions [3] member @g {interface_exports = array<i64: 48>}
    simulation.vpi_callable.decl 48 signature (i32) -> i32 directions [3]
    simulation.vpi_callable.decl 70 signature () -> () directions [] member @h {interface_exports = array<i64: 72, 73>, is_fork_join}
    simulation.vpi_callable.decl 71 signature () -> () directions [] member @h {interface_exports = array<i64>, is_fork_join}
    simulation.vpi_callable.decl 72 signature () -> () directions []
    simulation.vpi_callable.decl 73 signature () -> () directions []
    simulation.vpi_callable.decl 74 signature () -> () directions [] member @j {interface_exports = array<i64: 76>}
    simulation.vpi_callable.decl 75 signature () -> () directions [] member @j {interface_exports = array<i64: 76>}
    simulation.vpi_callable.decl 76 signature () -> () directions []
    simulation.vpi_callable.decl 80 signature (i64) -> i32 directions [1] member @different_f {interface_exports = array<i64: 90>}
    simulation.vpi_callable.decl 90 signature (i64) -> i32 directions [1]
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.vpi_call.decl @forkjoin id 15 in 44 scope 1 type 60 name "h" statement 11 within 11 {is_indirect}
    simulation.vpi_call.decl @different_call id 16 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @extern_task id 17 in 44 scope 1 type 60 name "j" {is_indirect}
    simulation.vpi_call.decl @dpi_f_call id 18 in 44 scope 1 type 19 name "dpi_f" {is_indirect}
    simulation.vpi_call.decl @dpi_t_call id 19 in 44 scope 1 type 60 name "dpi_t" {is_indirect}
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @export id 11 in 44 scope 1 type 19 name "g" {is_indirect}
    simulation.vpi_call.decl @no_target id 12 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @global id 13 in 60 scope 0 type 19 name "f" {is_indirect}
    simulation.vpi_call.decl @second id 14 in 64 scope 4 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @forkjoin member @h access 2
    simulation.vpi_interface_call.dispatch @different_call member @different_f access 0
    simulation.vpi_interface_call.dispatch @extern_task member @j access 0
    simulation.vpi_interface_call.dispatch @dpi_f_call member @dpi_f access 0
    simulation.vpi_interface_call.dispatch @dpi_t_call member @dpi_t access 0
    simulation.vpi_interface_call.dispatch @call member @f access 0
    simulation.vpi_interface_call.dispatch @export member @g access 2
    simulation.vpi_interface_call.dispatch @no_target member @empty access 0
    simulation.vpi_interface_call.dispatch @global member @f access 0
    simulation.vpi_interface_call.dispatch @second member @f access 1
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
  }
}
