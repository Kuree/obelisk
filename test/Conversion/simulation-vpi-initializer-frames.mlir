// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.InitializerFrames
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @initializer_frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.x"
    simulation.storage.decl 1 in 1 : i32 design hierarchy "top.Node.shared"
    simulation.class.decl @node id 1 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @shared of @node at 0 : i32 {is_static = true, is_weak = false}
    simulation.vpi_object.anchor @definition id 1 type 652 in 1 parent @top ordinal 0 hierarchy "top.Node" debug "Node" {backing = #simulation.vpi_backing<kind = class, symbol = @node>}
    simulation.vpi_typespec.decl @canonical id 0 in 1 owner @definition hierarchy "top.Node$type" debug "Node" {
      origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @node, range = [], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @member id 2 in 1 owner @canonical field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 1 : i64
    }
    simulation.vpi_class_member.decl @lexical id 3 in 1 owner @definition field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 1 : i64
    }
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.$static_initializer" {internal}
    simulation.code_unit.decl 42 in 1 function hierarchy "top.Node.shared.$static_initializer" {internal}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 60 in 1 function hierarchy "top.compute" debug "compute"
    simulation.code_unit.decl 61 in 1 function hierarchy "top.other" debug "other"
    simulation.vpi_initializer_binding 41 storage 0
    simulation.vpi_initializer_binding 42 storage 1
    simulation.vpi_call.decl @global_call id 20 in 41 scope 1 type 19 name "compute" callee 60
    simulation.vpi_call.decl @class_call id 21 in 42 scope 1 type 19 name "compute" callee 60
    simulation.vpi_call.decl @ordinary_call id 30 in 44 scope 1 type 19 name "compute" callee 60
    simulation.func @entry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
  }
}
