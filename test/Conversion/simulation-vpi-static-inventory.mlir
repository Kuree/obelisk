// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=read' \
// RUN:   | %python %S/Inputs/dump-design-database.py \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @static_inventory {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}

    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_object.anchor @pkg id 1 type 600 in 0 ordinal 1
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @class id 2 type 652 in 0 parent @pkg
        ordinal 0 hierarchy "pkg::C" debug "C"
    obelisk_sim.code_unit.decl 2 in 0 function
        hierarchy "pkg::C::method" debug "method"
    obelisk_sim.vpi_object.anchor @method id 3 type 20 in 0 parent @class
        ordinal 0 hierarchy "pkg::C::method" debug "method" {
      backing = #obelisk_sim.vpi_backing<kind = code_unit, id = 2 : i64>
    }

    obelisk_sim.vpi_typespec.decl @base_t id 0 in 1 owner @top
        hierarchy "top.base_t" debug "base_t" {
      target_type = #obelisk_sim.vpi_type<kind = packed_array,
          isSigned = false, isFourState = false, range = [7, 0], children = [
            #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>
          ], childNames = [], typedefAliases = [@base_t]>
    }
    obelisk_sim.vpi_typespec.decl @alias_t id 1 in 1 owner @top
        hierarchy "top.alias_t" debug "alias_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@alias_t, @base_t]>
    }
    obelisk_sim.vpi_typespec.decl @state_t id 2 in 1 owner @top
        hierarchy "top.state_t" debug "state_t" {
      origin = 2 : i32,
      source_type_identity = 7 : i64,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = true, name = "state_t", range = [], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [1, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    obelisk_sim.vpi_typespec.decl @enum_array_t id 3 in 1 owner @top
        hierarchy "top.enum_array_t" debug "enum_array_t" {
      target_type = #obelisk_sim.vpi_type<kind = packed_array,
          isSigned = false, isFourState = true, range = [3, 0], children = [
            #obelisk_sim.vpi_type<kind = enum, isSigned = false,
                isFourState = true, name = "state_t", range = [], children = [
                  #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                      isFourState = true, range = [1, 0], children = [],
                      childNames = []>
                ], childNames = []>
          ], childNames = [], typedefAliases = [@enum_array_t]>
    }
    obelisk_sim.vpi_typespec.decl @iface_t id 4 in 1 owner @top
        hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "@iface",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = []>
    }
    obelisk_sim.vpi_typespec.decl @iface_mp_t id 5 in 1 owner @top
        hierarchy "@iface" debug "iface.mp" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "@iface",
          symbol = @iface_mp_t, modport = "mp", range = [], children = [],
          childNames = []>
    }
    obelisk_sim.vpi_typespec.decl @anonymous_t id 6 in 1 owner @top
        hierarchy "top" debug "anonymous" {
      origin = 2 : i32,
      source_type_identity = 8 : i64,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = false, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    obelisk_sim.vpi_typespec.decl @record_t id 7 in 1 owner @top
        hierarchy "top.record_t" debug "record_t" {
      target_type = #obelisk_sim.vpi_type<kind = packed_struct,
          isSigned = false, isFourState = true, name = "record_t", range = [],
          children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>,
            #obelisk_sim.vpi_type<kind = enum, isSigned = false,
                isFourState = false, name = "nested_state", range = [],
                children = [
                  #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                      isFourState = false, range = [0, 0], children = [],
                      childNames = []>
                ], childNames = []>
          ], childNames = ["flag", "state"], isTagged = false,
          isSoft = false, bitWidth = 2 : i64, selectableWidth = 2 : i64,
          bitstreamWidth = 2 : i64, tagBits = 0 : i64,
          childOrdinals = [0, 1], childPackedOffsets = [1, 0],
          childRandTypes = [2, 3]>
    }
    obelisk_sim.vpi_enum_const.decl 0 enum @state_t ordinal 0
        name "IDLE" value "2'b00"
    obelisk_sim.vpi_enum_const.decl 1 enum @state_t ordinal 1
        name "RUN" value "2'b01"

    obelisk_sim.storage.decl 0 in 1 : i1 design hierarchy "top.value" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@alias_t]>
    }
    obelisk_sim.storage.decl 1 in 1 : i1 design hierarchy "top.anon_value" {
      obelisk_sim.vpi_source_type_identity = 8 : i64,
      vpi_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = false, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    obelisk_sim.storage.decl 2 in 1 :
        !obelisk_sim.unpacked_array<0 : 1 x
          !obelisk_sim.packed_array<7 : 4 x !obelisk_sim.logic<1>>>
        design hierarchy "top.indexed_value" {
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = true, range = [0, 1], children = [
            #obelisk_sim.vpi_type<kind = packed_array,
                isSigned = false, isFourState = true, range = [7, 4],
                children = [
                  #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                      isFourState = true, range = [0, 0], children = [],
                      childNames = []>
                ], childNames = []>
          ], childNames = []>
    }
    obelisk_sim.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// Static-only source anchors and typespec inventory use kind 9 and retain
// their exact generated VPI object kinds.
// CHECK: object name=pkg:: kind=9 vpi_kind=600 caps=0x0 id=3
// CHECK: object name=pkg::C kind=9 vpi_kind=652 caps=0x0 id=6
// CHECK: object name=pkg::C::method kind=7 vpi_kind=20 caps=0x80
// CHECK: object name=@iface kind=9 vpi_kind=906 caps=0x0 id=13
// CHECK: object name=@iface kind=9 vpi_kind=906 caps=0x0 id=16
// The anonymous typespec deliberately shares its physical scope's name. It is
// relation-only, so it must not make the immutable name index ambiguous.
// CHECK: object name=top kind=9 vpi_kind=633 caps=0x0 id=19
// CHECK: object name=top.alias_t kind=9 vpi_kind=640 caps=0x80 id=4
// CHECK: object name=top.base_t kind=9 vpi_kind=640 caps=0x80 id=1
// CHECK: object name=top.enum_array_t kind=9 vpi_kind=692 caps=0x80 id=10
// The physical image keeps the unpacked source-order range and the nested
// packed range used by vpi_handle_by_multi_index offset calculation.
// CHECK: object name=top.indexed_value kind=2 vpi_kind=116 {{.*}} width=8 range=[0:1] {{.*}} type_kind=2 type_flags=0x1 {{.*}} element_kind=2 element_flags=0x5 element_width=4 element_range=[7:4] child_kind=1 child_flags=0x5 child_width=1
// CHECK: object name=top.record_t kind=9 vpi_kind=638 caps=0x80
// CHECK: object name=top.state_t kind=9 vpi_kind=633 caps=0x0 id=7
// CHECK: object name=top.state_t::IDLE kind=9 vpi_kind=634 caps=0x0 id=2
// CHECK: object name=top.state_t::RUN kind=9 vpi_kind=634 caps=0x0 id=5
// The generated relation image exposes lexical ownership, declaration-order
// typedefs, the immediate typedef alias, interface instance links, enum
// members in source order, reverse enum ownership, and value typespec lookup.
// CHECK-DAG: relation {{.*}} source_type=0 mode=iterate selector=745 ordinal=0 {{.*}} source_name=$root target_name=top
// CHECK-DAG: relation {{.*}} source_type=0 mode=iterate selector=745 ordinal=1 {{.*}} source_name=$root target_name=pkg::
// CHECK-DAG: relation {{.*}} source_type=600 mode=iterate selector=652 ordinal=0 {{.*}} source_name=pkg:: target_name=pkg::C
// CHECK-DAG: relation {{.*}} source_type=652 mode=handle selector=84 ordinal=0 {{.*}} source_name=pkg::C target_name=pkg::
// CHECK-DAG: relation {{.*}} source_type=652 mode=iterate selector=92 ordinal=0 {{.*}} source_name=pkg::C target_name=pkg::C::method
// CHECK-DAG: relation {{.*}} source_type=652 mode=iterate selector=730 ordinal=0 {{.*}} source_name=pkg::C target_name=pkg::C::method
// CHECK-DAG: relation {{.*}} source_type=20 mode=handle selector=84 ordinal=0 {{.*}} source_name=pkg::C::method target_name=pkg::C
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=725 ordinal=0 {{.*}} source_name=top target_name=top.base_t
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=725 ordinal=1 {{.*}} source_name=top target_name=top.alias_t
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=725 ordinal=2 {{.*}} source_name=top target_name=top.enum_array_t
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=725 ordinal=3 {{.*}} source_name=top target_name=top.record_t
// CHECK-DAG: relation {{.*}} source_type=640 mode=handle selector=701 ordinal=0 {{.*}} source_name=top.alias_t target_name=top.base_t
// CHECK-DAG: relation {{.*}} source_type=640 mode=handle selector=745 ordinal=0 {{.*}} source_name=top.base_t target_name=top
// CHECK-DAG: relation {{.*}} source_type=633 mode=handle selector=745 ordinal=0 {{.*}} source_name=top.state_t target_name=top
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=605 ordinal=0 {{.*}} source_name=top target_name=@iface
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=605 ordinal=1 {{.*}} source_name=top target_name=@iface
// CHECK-DAG: relation {{.*}} source_type=906 mode=handle selector=745 ordinal=0 {{.*}} source_name=@iface target_name=top
// CHECK-DAG: relation {{.*}} source_type=906 mode=handle selector=745 ordinal=0 {{.*}} source_name=@iface target_name=top
// CHECK-DAG: relation {{.*}} source_type=633 mode=iterate selector=634 ordinal=0 {{.*}} source_name=top.state_t target_name=top.state_t::IDLE
// CHECK-DAG: relation {{.*}} source_type=633 mode=iterate selector=634 ordinal=1 {{.*}} source_name=top.state_t target_name=top.state_t::RUN
// CHECK-DAG: relation {{.*}} source_type=634 mode=handle selector=633 ordinal=0 {{.*}} source_name=top.state_t::IDLE target_name=top.state_t
// CHECK-DAG: relation {{.*}} source_type=634 mode=handle selector=633 ordinal=0 {{.*}} source_name=top.state_t::RUN target_name=top.state_t
// CHECK-DAG: relation {{.*}} source_type=620 mode=handle selector=605 ordinal=0 {{.*}} source_name=top.value target_name=top.alias_t
// CHECK-DAG: relation {{.*}} mode=handle selector=605 ordinal=0 {{.*}} source_name=top.anon_value target_name=top
// CHECK-DAG: relation {{.*}} source_type=633 mode=handle selector=745 ordinal=0 {{.*}} source_name=top target_name=top

// The semantic side graph retains recursive anonymous types without creating
// one static database object per range or member. Integral [0:0] bounds are
// intrinsic widths, not explicit VPI dimensions; only array nodes set 0x400.
// CHECK-DAG: semantic_type {{.*}} kind=17 flags=0x400 public_vpi_kind=640 {{.*}} range=[7:0]
// CHECK-DAG: semantic_type {{.*}} kind=19 flags=0x200 public_vpi_kind=638 {{.*}} name=record_t {{.*}} bit_width=2
// CHECK-DAG: semantic_type {{.*}} kind=3 flags=0x200 public_vpi_kind=641 {{.*}} range=[0:0]
// CHECK-DAG: semantic_type {{.*}} kind=10 flags=0x0 public_vpi_kind=633 {{.*}} name=nested_state
// CHECK-DAG: semantic_type {{.*}} kind=2 flags=0x0 public_vpi_kind=640 {{.*}} range=[0:0]
// CHECK-DAG: semantic_edge {{.*}} role=4 flags=0x2 ordinal=0 name=flag packed_offset=1
// CHECK-DAG: semantic_edge {{.*}} role=4 flags=0x3 ordinal=1 name=state packed_offset=0
// CHECK-DAG: semantic_edge {{.*}} role=1 {{.*}} ordinal=0 name= packed_offset=0
// CHECK-DAG: semantic_root {{.*}} object_name=top.record_t semantic_type={{[0-9]+}}
// CHECK-DAG: semantic_type {{.*}} kind=18 flags=0x600 public_vpi_kind=642 {{.*}} range=[0:1]
// CHECK-DAG: semantic_type {{.*}} kind=17 flags=0x600 public_vpi_kind=641 {{.*}} range=[7:4]
