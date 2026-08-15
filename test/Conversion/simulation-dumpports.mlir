// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-design-database.py \
// RUN:   | FileCheck %s --check-prefix=DATABASE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dumpports {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.d"
    obelisk_sim.storage.decl 0 in 1 : i8 design hierarchy "top.d.a"
    obelisk_sim.storage.decl 1 in 0 : i16 design hierarchy "top.backing"
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<1> design hierarchy "top.d.io"
    obelisk_sim.port.decl 0 in 1 source 0 net = false at 0 : i8 input ordinal 0 hierarchy "top.d.a" debug "a"
    obelisk_sim.port.decl 1 in 1 source 0 net = true at 0 : !obelisk_sim.logic<1> inout ordinal 1 hierarchy "top.d.io" debug "io"
    obelisk_sim.port.decl 2 in 1 source 1 net = false at 4 : i4 output ordinal 2 hierarchy "top.d.slice" debug "slice"
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
// DATABASE: object name=top.backing kind=2 caps=0x1 id=1 scope=top width=16 range=[15:0] state=8 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE: object name=top.d.a kind=2 caps=0x9 id=0 scope=top.d width=8 range=[7:0] state=0 type_kind=1 type_flags=0x4 port_ordinal=0
// DATABASE-NEXT: object name=top.d.io kind=3 caps=0x119 id=0 scope=top.d width=1 range=[0:0] state=24 type_kind=1 type_flags=0x5 port_ordinal=1
// DATABASE-NEXT: object name=top.d.slice kind=8 caps=0x211 id=2 scope=top.d width=4 range=[3:0] state=12 type_kind=1 type_flags=0x4 port_ordinal=2
