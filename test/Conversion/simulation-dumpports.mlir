// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.off \
// RUN:   OBELISK_TEST_VPI=off %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s --check-prefix=DATABASE < %t.off
// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.read \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s --check-prefix=VPI < %t.read

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dumpports {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.vpi_definition.decl @d_def type 32 name "d"
    obelisk_sim.vpi_definition_member.decl @d_a of @d_def type 28 ordinal 0
        name "a" direction input loc("ports.sv":2:10)
    obelisk_sim.vpi_definition_member.decl @d_io of @d_def type 28 ordinal 1
        name "io" direction inout loc("ports.sv":2:31)
    obelisk_sim.vpi_definition_member.decl @d_slice of @d_def type 28 ordinal 2
        name "slice" direction output loc("ports.sv":2:49)
    obelisk_sim.vpi_definition_member.decl @d_ref of @d_def type 28 ordinal 3
        name "r" direction ref loc("ports.sv":2:67)
    obelisk_sim.vpi_definition_member.decl @d_iface of @d_def type 28 ordinal 4
        name "iface" direction undefined loc("ports.sv":2:78)
    obelisk_sim.vpi_definition_specialization.decl @d_spec of @d_def
    obelisk_sim.vpi_definition_member.specialize @d_spec member @d_a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [7, 0], children = [], childNames = []>
    obelisk_sim.vpi_definition_member.specialize @d_spec member @d_io type
        #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    obelisk_sim.vpi_definition_member.specialize @d_spec member @d_slice type
        #obelisk_sim.vpi_type<kind = bit, isSigned = true,
          isFourState = false, range = [3, 0], children = [], childNames = []>
    obelisk_sim.vpi_definition_member.specialize @d_spec member @d_ref type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [7, 0], children = [], childNames = []>
    obelisk_sim.vpi_definition_member.specialize @d_spec member @d_iface type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.vpi_definition_member.bind scope 1 member @d_a expr
        <kind = storage, id = 0 : i64>
    obelisk_sim.vpi_definition_member.bind scope 1 member @d_io expr
        <kind = net, id = 0 : i64>
    obelisk_sim.vpi_definition_member.bind scope 2 member @d_a expr
        <kind = storage, id = 3 : i64>
    obelisk_sim.vpi_definition_member.bind scope 2 member @d_io expr
        <kind = net, id = 1 : i64>
    // RefObj identities are synthesized from the shared member plus the
    // instance binding. Only the ultimate actual target word is per-instance.
    obelisk_sim.vpi_definition_member.bind scope 1 member @d_ref expr
        <kind = storage, id = 3 : i64>
    obelisk_sim.vpi_definition_member.bind scope 2 member @d_ref expr
        <kind = storage, id = 0 : i64>
    obelisk_sim.vpi_definition_member.bind scope 3 member @d_ref expr
        <kind = storage, id = 3 : i64>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.d" vpi_kind 32
        definition @d_def specialization @d_spec
    // A second instance deliberately shares the definition and complete type
    // specialization without cloning any of the five IO declaration records.
    obelisk_sim.scope.decl 2 parent 0 hierarchy "top.e" vpi_kind 32
        definition @d_def specialization @d_spec
    obelisk_sim.scope.decl 3 parent 2 hierarchy "top.e.child" vpi_kind 32
        definition @d_def specialization @d_spec
    obelisk_sim.scope.decl 4 parent 1 hierarchy "top.d.child" vpi_kind 32
    obelisk_sim.vpi_object.anchor @top_d id 0 type 32 in 1 ordinal 0
        hierarchy "top.d" debug "d" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_typespec.decl @iface_t id 0 in 1 owner @top_d
        hierarchy "iface_t" debug "iface_t" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "iface_t",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = []>
    }
    // This typedef deliberately shares the raw interface identity. It must
    // not replace @iface_t as the canonical semantic identity target.
    obelisk_sim.vpi_typespec.decl @iface_alias_t id 1 in 1 owner @top_d
        hierarchy "top.d.iface_alias_t" debug "iface_alias_t" {
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "iface_t",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = [], typedefAliases = [@iface_alias_t]>
    }
    obelisk_sim.vpi_typespec.decl @base_t id 2 in 1 owner @top_d
        hierarchy "top.d.base_t" debug "base_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@base_t]>
    }
    obelisk_sim.vpi_typespec.decl @alias_t id 3 in 1 owner @top_d
        hierarchy "top.d.alias_t" debug "alias_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@alias_t, @base_t]>
    }
    obelisk_sim.storage.decl 0 in 1 : i8 design hierarchy "top.d.a" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [7, 0], children = [], childNames = []>
    }
    obelisk_sim.storage.decl 1 in 0 : i16 design hierarchy "top.backing"
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<1> design hierarchy "top.d.io" {
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.storage.decl 2 in 1 : i8 design debug "zzBacking"
    // Child ports keep their local low-side objects while their high-side
    // connections point back to the parent instance's RefObj.
    obelisk_sim.storage.decl 4 in 4 : i8 design hierarchy "top.d.child.q"
    obelisk_sim.storage.decl 5 in 4 : i16 design hierarchy "top.d.child.zouter"
    obelisk_sim.storage.decl 3 in 2 : i8 design hierarchy "top.e.a" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [7, 0], children = [], childNames = []>
    }
    obelisk_sim.net.decl 1 in 2 : !obelisk_sim.logic<1> design hierarchy "top.e.io" {
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.port.decl 0 in 1 source 0 net = false at 0 : i8 input ordinal 0 hierarchy "top.d.a" debug "a"
    obelisk_sim.port.decl 1 in 1 source 0 net = true at 0 : !obelisk_sim.logic<1> inout ordinal 1 hierarchy "top.d.io" debug "io"
    obelisk_sim.port.decl 2 in 1 source 1 net = false at 4 : i4 output ordinal 2 hierarchy "top.d.slice" debug "slice"
    obelisk_sim.port.decl 3 in 1 source 0 net = false at 0 : i8 inout ordinal 3 hierarchy "top.d.p" debug "p"
    obelisk_sim.port.decl 4 in 4 source 4 net = false at 0 : i8 input ordinal 4 hierarchy "top.d.child.q" debug "q"
    obelisk_sim.port.decl 5 in 4 source 5 net = false at 0 : i16 input ordinal 5 hierarchy "top.d.child.zouter" debug "zouter"
    obelisk_sim.port.decl 6 in 2 source 3 net = false at 0 : i8 inout ordinal 3 hierarchy "top.e.r" debug "r"
    obelisk_sim.port.decl 7 in 3 source 3 net = false at 0 : i8 inout ordinal 0 hierarchy "top.e.child.r" debug "r"
    // The synthetic RefObj remains allocation-free. These sparse rows retain
    // its low-side declaring port separately from downstream high-side uses.
    obelisk_sim.vpi_definition_member.relation scope 1 member @d_ref
        selector 44 iterate ordinal 0 to <kind = port, id = 3 : i64>
    obelisk_sim.vpi_definition_member.relation scope 1 member @d_ref
        selector 98 iterate ordinal 0 to <kind = port, id = 4 : i64>
    obelisk_sim.vpi_definition_member.relation scope 1 member @d_ref
        selector 98 iterate ordinal 1 to <kind = port, id = 5 : i64>
    obelisk_sim.vpi_definition_member.relation scope 2 member @d_ref
        selector 44 iterate ordinal 0 to <kind = port, id = 6 : i64>
    obelisk_sim.vpi_definition_member.relation scope 2 member @d_ref
        selector 98 iterate ordinal 0 to <kind = port, id = 7 : i64>
    // The nested port is both a downstream use of top.e.r and the declaring
    // port of its own top.e.child.r RefObj.
    obelisk_sim.vpi_definition_member.relation scope 3 member @d_ref
        selector 44 iterate ordinal 0 to <kind = port, id = 7 : i64>
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.dump"
    obelisk_sim.func @dump(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %path = obelisk_sim.string.literal "ports.evcd"
      %scope = obelisk_sim.string.literal "top.d"
      %empty = obelisk_sim.string.literal ""
      %scale = arith.constant -9 : i32
      %zero = arith.constant 0 : i64
      %limit = arith.constant 4096 : i64
      obelisk_sim.dump.ports %ctx, %path, %scope, %scale :
          (!obelisk_sim.context, !obelisk_sim.string, !obelisk_sim.string, i32) -> ()
      obelisk_sim.dump.ports_control %ctx, %path, %zero {action = 0 : i32} :
          (!obelisk_sim.context, !obelisk_sim.string, i64) -> ()
      obelisk_sim.dump.ports_control %ctx, %empty, %zero {action = 2 : i32} :
          (!obelisk_sim.context, !obelisk_sim.string, i64) -> ()
      obelisk_sim.dump.ports_control %ctx, %path, %limit {action = 4 : i32} :
          (!obelisk_sim.context, !obelisk_sim.string, i64) -> ()
      obelisk_sim.return
    }
  }
}

// NATIVE-LABEL: llvm.func @dump
// NATIVE: llvm.call @obelisk_rt_v1_dump_ports
// NATIVE: llvm.call @obelisk_rt_v1_dump_ports_control
// NATIVE: llvm.call @obelisk_rt_v1_dump_ports_control
// NATIVE: llvm.call @obelisk_rt_v1_dump_ports_control
// NATIVE-NOT: obelisk_sim.port.decl

// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010232 inputs=3 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010233 inputs=2 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010233 inputs=2 outputs=0 flags=2
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010233 inputs=2 outputs=0 flags=4
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010232 inputs={{\[[0-9]+, [0-9]+, [0-9]+\]}} outputs=[]

// Direct whole-source ports retain the storage/net record and gain exact
// direction/order metadata. A sliced alias remains a distinct port record in
// its declaring module scope and points at the canonical source bit range.
// DATABASE: object name=top.backing kind=2 vpi_kind=48 caps=0x1 id=1 scope=top width=16 range=[15:0] state=8 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE: object name=top.d.a kind=2 vpi_kind=620 caps=0x9 id=0 scope=top.d width=8 range=[7:0] state=0 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE-NEXT: object name=top.d.io kind=3 vpi_kind=36 caps=0x119 id=0 scope=top.d width=1 range=[0:0] state=24 type_kind=1 type_flags=0x5 port_ordinal=1
// DATABASE-NEXT: object name=top.d.p kind=8 vpi_kind=44 caps=0x319 id=3 scope=top.d width=8 range=[7:0] state=0 type_kind=1 type_flags=0x4 port_ordinal=3
// DATABASE-NEXT: object name=top.d.slice kind=8 vpi_kind=44 caps=0x211 id=2 scope=top.d width=4 range=[3:0] state=12 type_kind=1 type_flags=0x4 port_ordinal=2
// DATABASE-NEXT: object name=zzBacking kind=2 vpi_kind=48 caps=0x1 id=2 scope=top.d width=8 range=[7:0] state=32 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE-NEXT: object name=top.e.a kind=2 vpi_kind=620 caps=0x1 id=3 scope=top.e width=8 range=[7:0] state=64 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE-NEXT: object name=top.e.io kind=3 vpi_kind=36 caps=0x1 id=1 scope=top.e width=1 range=[0:0] state=72 type_kind=1 type_flags=0x5 port_ordinal=0
// DATABASE: object name=top.d.child.q kind=2 vpi_kind=48 caps=0x409 id=4 scope=top.d.child width=8 range=[7:0] state=40 type_kind=1 type_flags=0x4 port_ordinal=4
// DATABASE-NEXT: object name=top.d.child.zouter kind=2 vpi_kind=48 caps=0x509 id=5 scope=top.d.child width=16 range=[15:0] state=48 type_kind=1 type_flags=0x4 port_ordinal=5
// DATABASE-NOT: relation

// Static relation records are inferred from the existing scope ownership;
// no relation declaration ops are needed. A direct whole-source port has a
// distinct vpiPort identity sharing its variable/net state, while name lookup
// remains canonical on the storage or net record.
// VPI: object name=iface_t kind=9 vpi_kind=906
// VPI: object name=top.d.a kind=2 vpi_kind=620
// VPI-NEXT: object name=top.d.a kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.alias_t kind=9 vpi_kind=640
// VPI-NEXT: object name=top.d.base_t kind=9 vpi_kind=640
// VPI-NEXT: object name=top.d.iface_alias_t kind=9 vpi_kind=906
// VPI-NEXT: object name=top.d.io kind=3 vpi_kind=36
// VPI-NEXT: object name=top.d.io kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.p kind=8 vpi_kind=44 caps=0x399
// VPI-NEXT: object name=top.d.slice kind=8 vpi_kind=44
// VPI-NEXT: object name=zzBacking kind=2 vpi_kind=48
// VPI-NEXT: object name=top.e.a kind=2 vpi_kind=620
// VPI-NEXT: object name=top.e.io kind=3 vpi_kind=36
// VPI: object name=top.d.child.q kind=2 vpi_kind=48
// VPI-NEXT: object name=top.d.child.q kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.child.zouter kind=2 vpi_kind=48
// VPI-NEXT: object name=top.d.child.zouter kind=8 vpi_kind=44

// Per-instance expression endpoints and reverse relations reuse the shared
// definition members. The inverse is a four-byte permutation sorted by its
// dereferenced physical port, not a second copy of the endpoint token.
// VPI: definition_binding source_table=0 source=1 definition=0 specialization=0 source_name=top.d first_member_endpoint=0
// VPI-NEXT: definition_binding source_table=0 source=2 definition=0 specialization=0 source_name=top.e first_member_endpoint=5
// VPI-NEXT: definition_binding source_table=0 source=3 definition=0 specialization=0 source_name=top.e.child first_member_endpoint=10
// VPI-NEXT: definition_member_endpoint index=0 binding=0 member=0 member_name=a target_table=1 target=3 target_name=top.d.a
// VPI-NEXT: definition_member_endpoint index=1 binding=0 member=1 member_name=io target_table=1 target=8 target_name=top.d.io
// VPI-NEXT: definition_member_endpoint index=2 binding=0 member=2 member_name=slice target=none
// VPI-NEXT: definition_member_endpoint index=3 binding=0 member=3 member_name=r target_table=1 target=13 target_name=top.e.a
// VPI-NEXT: definition_member_endpoint index=4 binding=0 member=4 member_name=iface target=none
// VPI-NEXT: definition_member_endpoint index=5 binding=1 member=0 member_name=a target_table=1 target=13 target_name=top.e.a
// VPI-NEXT: definition_member_endpoint index=6 binding=1 member=1 member_name=io target_table=1 target=14 target_name=top.e.io
// VPI-NEXT: definition_member_endpoint index=7 binding=1 member=2 member_name=slice target=none
// VPI-NEXT: definition_member_endpoint index=8 binding=1 member=3 member_name=r target_table=1 target=3 target_name=top.d.a
// VPI-NEXT: definition_member_endpoint index=9 binding=1 member=4 member_name=iface target=none
// VPI-NEXT: definition_member_endpoint index=10 binding=2 member=0 member_name=a target=none
// VPI-NEXT: definition_member_endpoint index=11 binding=2 member=1 member_name=io target=none
// VPI-NEXT: definition_member_endpoint index=12 binding=2 member=2 member_name=slice target=none
// VPI-NEXT: definition_member_endpoint index=13 binding=2 member=3 member_name=r target_table=1 target=13 target_name=top.e.a
// VPI-NEXT: definition_member_endpoint index=14 binding=2 member=4 member_name=iface target=none
// VPI-NEXT: definition_member_instance_relation index=0 binding=0 member=3 member_name=r selector=44 mode=1 targets=[0:1)
// VPI-NEXT: definition_member_instance_relation index=1 binding=0 member=3 member_name=r selector=98 mode=1 targets=[1:3)
// VPI-NEXT: definition_member_instance_relation index=2 binding=1 member=3 member_name=r selector=44 mode=1 targets=[3:4)
// VPI-NEXT: definition_member_instance_relation index=3 binding=1 member=3 member_name=r selector=98 mode=1 targets=[4:5)
// VPI-NEXT: definition_member_instance_relation index=4 binding=2 member=3 member_name=r selector=44 mode=1 targets=[5:6)
// VPI-NEXT: definition_member_instance_relation_target index=0 relation=0 target_table=1 target=10 target_name=top.d.p
// VPI-NEXT: definition_member_instance_relation_target index=1 relation=1 target_table=1 target=18 target_name=top.d.child.q
// VPI-NEXT: definition_member_instance_relation_target index=2 relation=1 target_table=1 target=20 target_name=top.d.child.zouter
// VPI-NEXT: definition_member_instance_relation_target index=3 relation=2 target_table=1 target=15 target_name=top.e.r
// VPI-NEXT: definition_member_instance_relation_target index=4 relation=3 target_table=1 target=16 target_name=top.e.child.r
// VPI-NEXT: definition_member_instance_relation_target index=5 relation=4 target_table=1 target=16 target_name=top.e.child.r
// VPI-NEXT: definition_member_instance_relation_inverse index=0 target_index=0 target_table=1 target=10 target_name=top.d.p
// VPI-NEXT: definition_member_instance_relation_inverse index=1 target_index=3 target_table=1 target=15 target_name=top.e.r
// VPI-NEXT: definition_member_instance_relation_inverse index=2 target_index=4 target_table=1 target=16 target_name=top.e.child.r
// VPI-NEXT: definition_member_instance_relation_inverse index=3 target_index=5 target_table=1 target=16 target_name=top.e.child.r
// VPI-NEXT: definition_member_instance_relation_inverse index=4 target_index=1 target_table=1 target=18 target_name=top.d.child.q
// VPI-NEXT: definition_member_instance_relation_inverse index=5 target_index=2 target_table=1 target=20 target_name=top.d.child.zouter

// The compact inverse owns p's low connection, so no ordinary selector-80 row
// is emitted for p. Child ports retain ordinary local low connections.
// VPI: relation source_table=1 source=10 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=10 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI: relation source_table=1 source=18 source_type=44 mode=handle selector=80 ordinal=0 target_table=1 target=17
// VPI: relation source_table=1 source=20 source_type=44 mode=handle selector=80 ordinal=0 target_table=1 target=19
