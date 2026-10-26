// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.LiveClassMembers

// IEEE 1800-2023 37.31-37.33: declarations and receiver-bound properties
// share field metadata while preserving value access and object lifetimes.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @class_members {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.class.decl @node id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @number of @node at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.class.field @bits of @node at 1 : !simulation.packed_array<7 : 0 x i1> {
      is_static = false, is_weak = false}
    simulation.class.field @flags of @node at 2 : !simulation.logic<8> {
      is_static = false, is_weak = false}
    simulation.class.field @next of @node at 3 : !simulation.class_handle<@node> {
      is_static = false, is_weak = false}
    simulation.class.field @shared of @node at 4 : i32 {
      is_static = true, is_weak = false}
    simulation.vpi_object.anchor @definition id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.Node" debug "Node" {
      backing = #simulation.vpi_backing<kind = class, symbol = @node>
    }
    simulation.vpi_typespec.decl @canonical id 0 in 1 owner @definition
        hierarchy "top.Node$type" debug "Node" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @node, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @number_member id 2 in 1 owner @canonical
        field @number hierarchy "top.Node.number" debug "number" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @bits_member id 3 in 1 owner @canonical
        field @bits hierarchy "top.Node.bits" debug "bits" {
      vpi_type = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false, range = [7, 0], children = [#simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>], childNames = []>
    }
    simulation.vpi_class_member.decl @flags_member id 4 in 1 owner @canonical
        field @flags hierarchy "top.Node.flags" debug "flags" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @next_member id 5 in 1 owner @canonical
        field @next hierarchy "top.Node.next" debug "next" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @node, range = [], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @shared_member id 6 in 1 owner @canonical
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 2 : i64
    }
    simulation.vpi_class_member.decl @number_lexical id 7 in 1 owner @definition
        field @number hierarchy "top.Node.number" debug "number" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @shared_lexical id 8 in 1 owner @definition
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 2 : i64
    }
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@node> design
        hierarchy "top.first" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @node, range = [], children = [], childNames = []>
    }
    simulation.storage.decl 1 in 1 : !simulation.class_handle<@node> design
        hierarchy "top.second" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @node, range = [], children = [], childNames = []>
    }
    simulation.storage.decl 2 in 1 : i32 design hierarchy "top.Node.shared" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
