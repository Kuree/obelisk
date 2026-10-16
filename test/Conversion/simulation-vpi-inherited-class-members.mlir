// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.InheritedClassMembers

// IEEE 1800-2023 37.32-37.33: inherited property lookup binds the receiver,
// methods hide by exact identifier, and static methods have no receiver.
!record = !simulation.packed_struct<[
  #simulation.field<name = "value", type = i32, ordinal = 0, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @inherited_class_members {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.class.decl @base id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @derived id 2 extends @base {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @number of @base at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.class.field @escaped of @base at 1 : i32 {
      is_static = false, is_weak = false}
    simulation.class.field @flags of @base at 2 : !simulation.logic<8> {
      is_static = false, is_weak = false}
    simulation.class.field @record of @base at 3 : !record {
      is_static = false, is_weak = false}
    simulation.class.field @own of @derived at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_object.anchor @base_definition id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.Base" debug "Base" {
      backing = #simulation.vpi_backing<kind = class, symbol = @base>
    }
    simulation.vpi_object.anchor @derived_definition id 2 type 652 in 1 parent @top
        ordinal 1 hierarchy "top.Derived" debug "Derived" {
      backing = #simulation.vpi_backing<kind = class, symbol = @derived>
    }
    simulation.vpi_typespec.decl @base_type id 0 in 1 owner @base_definition
        hierarchy "top.Base$type" debug "Base" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @base, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @derived_type id 1 in 1 owner @derived_definition
        hierarchy "top.Derived$type" debug "Derived" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @derived, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @number_member id 3 in 1 owner @base_type
        field @number hierarchy "top.Base.number" debug "number" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @escaped_member id 4 in 1 owner @base_type
        field @escaped hierarchy "top.Base.\\payload.value " debug "payload.value" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @flags_member id 5 in 1 owner @base_type
        field @flags hierarchy "top.Base.flags" debug "flags" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @own_member id 6 in 1 owner @derived_type
        field @own hierarchy "top.Derived.own" debug "own" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @record_member id 11 in 1 owner @base_type
        field @record hierarchy "top.Base.record" debug "record" {
      vpi_type = #simulation.vpi_type<kind = packed_struct, isSigned = false, isFourState = false, range = [], children = [#simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>], childNames = ["value"], isTagged = false, isSoft = false, bitWidth = 32 : i64, selectableWidth = 32 : i64, bitstreamWidth = 32 : i64, tagBits = 0 : i64, childOrdinals = [0], childPackedOffsets = [0]>
    }
    simulation.class.method @base_visit of @base implemented_by @base_body :
        (!simulation.context, !simulation.class_handle<@base>) -> () {
      is_final = false, is_pure = false, is_static = false,
      is_task = false, is_virtual = false, debug_name = "visit"
    }
    simulation.class.method @dotted_visit of @base implemented_by @base_body :
        (!simulation.context, !simulation.class_handle<@base>) -> () {
      is_final = false, is_pure = false, is_static = false,
      is_task = false, is_virtual = false, debug_name = "other.visit"
    }
    simulation.class.method @static_ping of @base implemented_by @static_body :
        (!simulation.context) -> () {
      is_final = false, is_pure = false, is_static = true,
      is_task = false, is_virtual = false, debug_name = "ping"
    }
    simulation.class.method @derived_visit of @derived implemented_by @derived_body :
        (!simulation.context, !simulation.class_handle<@derived>) -> () {
      is_final = false, is_pure = false, is_static = false,
      is_task = false, is_virtual = false, debug_name = "visit"
    }
    simulation.vpi_object.anchor @base_method id 7 type 20 in 1 parent @base_type
        ordinal 0 hierarchy "top.Base.visit" debug "visit" {
      class_method = @base_visit
    }
    simulation.vpi_object.anchor @dotted_method id 8 type 20 in 1 parent @base_type
        ordinal 1 hierarchy "top.Base.\\other.visit " debug "other.visit" {
      class_method = @dotted_visit
    }
    simulation.vpi_object.anchor @static_method id 9 type 20 in 1 parent @base_type
        ordinal 2 hierarchy "top.Base.ping" debug "ping" {
      class_method = @static_ping
    }
    simulation.vpi_object.anchor @derived_method id 10 type 20 in 1 parent @derived_type
        ordinal 0 hierarchy "top.Derived.visit" debug "visit" {
      class_method = @derived_visit
    }
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@derived> design
        hierarchy "top.obj" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @derived, range = [],
          children = [], childNames = []>
    }
    simulation.storage.decl 1 in 1 : !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@derived>> design
        hierarchy "top.objects" {
      vpi_type = #simulation.vpi_type<kind = unpacked_array, isSigned = false,
          isFourState = false, range = [0, 1],
          children = [#simulation.vpi_type<kind = class, isSigned = false,
              isFourState = false, symbol = @derived, range = [],
              children = [], childNames = []>], childNames = []>
    }
    simulation.vpi_relation.decl <kind = symbol, symbol = @derived_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 2 in 1 function hierarchy "top.Base.body"
    simulation.code_unit.decl 3 in 1 function hierarchy "top.Base.static_body"
    simulation.code_unit.decl 4 in 1 function hierarchy "top.Derived.body"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
    simulation.func private @base_body(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@base> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
    simulation.func private @static_body(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }
    simulation.func private @derived_body(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@derived> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
  }
}
