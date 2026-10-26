// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump \
// RUN:   %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

// Class properties preserve declared typedef and anonymous enum identity.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @member_types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @aliased of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.class.field @state of @cls at 1 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_object.anchor @definition id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.vpi_typespec.decl @canonical id 0 in 1 owner @definition
        hierarchy "top.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @integer_alias id 1 in 1 owner @canonical
        hierarchy "top.C.integer_alias" debug "integer_alias" {
      target_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = [],
        typedefAliases = [@integer_alias]>
    }
    simulation.vpi_typespec.decl @state_type id 2 in 1 owner @canonical
        hierarchy "top.C.state_type" debug "state_type" {
      origin = 2 : i32, source_type_identity = 7 : i64,
      target_type = #simulation.vpi_type<kind = enum, name = "anonymous", isSigned = true,
        isFourState = false, range = [], children = [
          #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
            range = [31, 0], children = [], childNames = []>
        ], childNames = []>
    }
    simulation.vpi_class_member.decl @aliased_member id 2 in 1 owner @canonical
        field @aliased hierarchy "top.C.aliased" debug "aliased" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = [],
        typedefAliases = [@integer_alias]>
    }
    simulation.vpi_class_member.decl @state_member id 3 in 1 owner @canonical
        field @state hierarchy "top.C.state" debug "state" {
      simulation.vpi_source_type_identity = 7 : i64,
      vpi_type = #simulation.vpi_type<kind = enum, name = "anonymous", isSigned = true,
        isFourState = false, range = [], children = [
          #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
            range = [31, 0], children = [], childNames = []>
        ], childNames = []>
    }
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// CHECK-DAG: relation {{.*}} mode=handle selector=605 ordinal=0 {{.*}} source_name=top.C.aliased target_name=top.C.integer_alias
// CHECK-DAG: relation {{.*}} mode=handle selector=605 ordinal=0 {{.*}} source_name=top.C.state target_name=top.C.state_type
