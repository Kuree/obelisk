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
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.d"
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
    obelisk_sim.storage.decl 0 in 1 : i8 design hierarchy "top.d.a"
    obelisk_sim.storage.decl 1 in 0 : i16 design hierarchy "top.backing"
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<1> design hierarchy "top.d.io"
    obelisk_sim.storage.decl 2 in 1 : i8 design debug "zzBacking"
    obelisk_sim.port.decl 0 in 1 source 0 net = false at 0 : i8 input ordinal 0 hierarchy "top.d.a" debug "a"
    obelisk_sim.port.decl 1 in 1 source 0 net = true at 0 : !obelisk_sim.logic<1> inout ordinal 1 hierarchy "top.d.io" debug "io"
    obelisk_sim.port.decl 2 in 1 source 1 net = false at 4 : i4 output ordinal 2 hierarchy "top.d.slice" debug "slice"
    obelisk_sim.port.decl 3 in 1 source 0 net = false at 0 : i8 input ordinal 3 hierarchy "top.d.p" debug "p"
    obelisk_sim.port.decl 4 in 1 source 2 net = false at 0 : i8 input ordinal 4 hierarchy "top.d.q" debug "q"
    obelisk_sim.port.decl 5 in 1 source 1 net = false at 0 : i16 input ordinal 5 hierarchy "top.d.zouter" debug "zouter"
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
// DATABASE: object name=top.d.a kind=2 vpi_kind=48 caps=0x9 id=0 scope=top.d width=8 range=[7:0] state=0 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE-NEXT: object name=top.d.io kind=3 vpi_kind=36 caps=0x119 id=0 scope=top.d width=1 range=[0:0] state=24 type_kind=1 type_flags=0x5 port_ordinal=1
// DATABASE-NEXT: object name=top.d.p kind=8 vpi_kind=44 caps=0x309 id=3 scope=top.d width=8 range=[7:0] state=0 type_kind=1 type_flags=0x4 port_ordinal=3
// DATABASE-NEXT: object name=top.d.q kind=8 vpi_kind=44 caps=0x409 id=4 scope=top.d width=8 range=[7:0] state=32 type_kind=1 type_flags=0x4 port_ordinal=4
// DATABASE-NEXT: object name=top.d.slice kind=8 vpi_kind=44 caps=0x211 id=2 scope=top.d width=4 range=[3:0] state=12 type_kind=1 type_flags=0x4 port_ordinal=2
// DATABASE-NEXT: object name=top.d.zouter kind=8 vpi_kind=44 caps=0x509 id=5 scope=top.d width=16 range=[15:0] state=8 type_kind=1 type_flags=0x4 port_ordinal=5
// DATABASE-NEXT: object name=zzBacking kind=2 vpi_kind=48 caps=0x1 id=2 scope=top.d width=8 range=[7:0] state=32 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE-NOT: relation

// Static relation records are inferred from the existing scope ownership;
// no relation declaration ops are needed. A direct whole-source port has a
// distinct vpiPort identity sharing its variable/net state, while name lookup
// remains canonical on the storage or net record.
// VPI: object name=iface_t kind=9 vpi_kind=906
// VPI: object name=top.d.a kind=2 vpi_kind=48
// VPI-NEXT: object name=top.d.a kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.alias_t kind=9 vpi_kind=640
// VPI-NEXT: object name=top.d.base_t kind=9 vpi_kind=640
// VPI-NEXT: object name=top.d.iface_alias_t kind=9 vpi_kind=906
// VPI-NEXT: object name=top.d.io kind=3 vpi_kind=36
// VPI-NEXT: object name=top.d.io kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.p kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.q kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.slice kind=8 vpi_kind=44
// VPI-NEXT: object name=top.d.zouter kind=8 vpi_kind=44
// VPI-NEXT: object name=zzBacking kind=2 vpi_kind=48
// VPI: relation source_table=0 source=1 source_type=32 mode=iterate selector=36 ordinal=0 target_table=1 target=8
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=44 ordinal=0 target_table=1 target=4
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=44 ordinal=1 target_table=1 target=9
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=44 ordinal=2 target_table=1 target=10
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=44 ordinal=3 target_table=1 target=11
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=44 ordinal=4 target_table=1 target=12
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=44 ordinal=5 target_table=1 target=13
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=48 ordinal=0 target_table=1 target=3
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=48 ordinal=1 target_table=1 target=14
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=100 ordinal=0 target_table=1 target=3
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=100 ordinal=1 target_table=1 target=14
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=605 ordinal=0 target_table=1 target=2
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=725 ordinal=0 target_table=1 target=7
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=725 ordinal=1 target_table=1 target=6
// VPI-NEXT: relation source_table=0 source=1 source_type=32 mode=iterate selector=725 ordinal=2 target_table=1 target=5
// VPI-NEXT: relation source_table=1 source=2 source_type=906 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=3 source_type=48 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=3 source_type=48 mode=handle selector=84 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=3 source_type=48 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=4 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=4 source_type=44 mode=handle selector=80 ordinal=0 target_table=1 target=3
// VPI-NEXT: relation source_table=1 source=4 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=5 source_type=640 mode=handle selector=701 ordinal=0 target_table=1 target=6
// VPI-NEXT: relation source_table=1 source=5 source_type=640 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=6 source_type=640 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=7 source_type=906 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=9 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=9 source_type=44 mode=handle selector=80 ordinal=0 target_table=1 target=8
// VPI-NEXT: relation source_table=1 source=9 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=10 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=10 source_type=44 mode=handle selector=80 ordinal=0 target_table=1 target=3
// VPI-NEXT: relation source_table=1 source=10 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=11 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=11 source_type=44 mode=handle selector=80 ordinal=0 target_table=1 target=14
// VPI-NEXT: relation source_table=1 source=11 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=12 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=12 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=13 source_type=44 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=13 source_type=44 mode=handle selector=745 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=14 source_type=48 mode=handle selector=32 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=14 source_type=48 mode=handle selector=84 ordinal=0 target_table=0 target=1
// VPI-NEXT: relation source_table=1 source=14 source_type=48 mode=handle selector=745 ordinal=0 target_table=0 target=1
