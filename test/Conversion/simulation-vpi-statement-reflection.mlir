// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=read' \
// RUN:   | %python %S/Inputs/dump-design-database.py \
// RUN:   | FileCheck %s --check-prefix=DATABASE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-design-database.py \
// RUN:   | FileCheck %s --check-prefix=WAVEFORM

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @statement_reflection {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      definition_loc = loc("definition.sv":27:4)
    } loc("use.sv":3:2)
    // An omitted intrinsic kind is derived from interface metadata.
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    obelisk_sim.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    // Exact kinds are intrinsic even when a code unit has no statement edge.
    obelisk_sim.code_unit.decl 20 in 0 always hierarchy "top.zalways"
    obelisk_sim.code_unit.decl 30 in 0 final hierarchy "top.zfinal"
    obelisk_sim.code_unit.decl 40 in 0 function hierarchy "top.zfunction"
    // Infrastructure kinds are intrinsically internal; no redundant marker is
    // needed for a valid kind-zero/cap-internal compact record.
    obelisk_sim.code_unit.decl 50 in 0 root_initializer hierarchy "top.zinternal"
    obelisk_sim.code_unit.decl 55 in 0 continuous hierarchy "top.zinternal_continuous"
    obelisk_sim.code_unit.decl 60 in 0 task hierarchy "top.ztask"

    // Deliberately reverse declaration and site order. The wire inventory is
    // normalized by stable ID, not by mutable MLIR block order or source loc.
    obelisk_sim.statement.decl 200 in 10 scope 0 type 15 parent 100 loc("test.sv":9:3)
    obelisk_sim.statement.decl 100 in 10 scope 0 type 33 name "body" {is_protected, is_scope} loc("test.sv":8:1)
    // Scope-owned continuous assignments are owned directly by the exact
    // elaborated scope and therefore omit a behavioral code-unit owner.
    obelisk_sim.statement.decl 50 scope 0 type 8 loc("test.sv":7:1)
    // Non-cbStmt statement kinds are still traversal-visible identities and
    // correctly have no semantic callback-site record.
    obelisk_sim.statement.decl 150 in 10 scope 0 type 38 parent 100 loc("test.sv":8:7)
    obelisk_sim.statement.decl 250 in 10 scope 0 type 38 parent 200 loc("test.sv":9:9)
    // A task is itself the effective VPI scope of its top-level statements.
    obelisk_sim.statement.decl 300 in 60 scope 0 type 38 loc("test.sv":11:3)
    obelisk_sim.statement_site.decl 1200 on 200 phase 2
    obelisk_sim.statement_site.decl 1000 on 100 phase 0
    obelisk_sim.statement_site.decl 1100 on 200 phase 1

    // Deliberately reverse semantic edge order as well. The image sorts by
    // table/index/selector/ordinal for zero-copy query ranges.
    obelisk_sim.vpi_statement_relation.decl statement 100 type 33 selector 104 ordinal 1 modes 2 to 200
    obelisk_sim.vpi_statement_relation.decl code_unit 10 type 24 selector 104 ordinal 0 modes 1 to 100
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 50
    obelisk_sim.vpi_statement_relation.decl statement 100 type 33 selector 104 ordinal 0 modes 2 to 150
    obelisk_sim.vpi_statement_relation.decl statement 200 type 15 selector 75 ordinal 0 modes 3 to 250
    obelisk_sim.vpi_statement_relation.decl code_unit 60 type 59 selector 104 ordinal 0 modes 1 to 300

    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 10 : i64} {
      // Force a VCD-only reflection image in the vpi=off run. Statement
      // inventory must still be absent from that image.
      %path = obelisk_sim.bytes.constant "/dev/null"
      obelisk_sim.dump.open %ctx, %path :
          (!obelisk_sim.context, !obelisk_sim.bytes) -> ()
      obelisk_sim.return
    }
  }
}

// DATABASE: scope name=top kind=1 vpi_kind=32 caps=0x4 id=0
// DATABASE-NEXT: scope name=top.bus kind=1 vpi_kind=601 caps=0x4 id=1
// DATABASE: object name=top.initial kind=5 vpi_kind=24 caps=0x0 id=10 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE-NEXT: object name=top.zalways kind=5 vpi_kind=1 caps=0x0 id=20 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE-NEXT: object name=top.zfinal kind=5 vpi_kind=676 caps=0x0 id=30 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE-NEXT: object name=top.zfunction kind=7 vpi_kind=20 caps=0x0 id=40 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE-NEXT: object name=top.zinternal kind=5 vpi_kind=0 caps=0x20 id=50 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE-NEXT: object name=top.zinternal_continuous kind=5 vpi_kind=0 caps=0x20 id=55 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE-NEXT: object name=top.ztask kind=5 vpi_kind=59 caps=0x0 id=60 scope=top width=0 range=[0:0] state=0 type_kind=0 type_flags=0x0 port_ordinal=0
// DATABASE: statement id=50 owner=4294967295 scope=0 parent=4294967295 type=8 flags=0x0 source=test.sv:7:1 name=
// DATABASE-NEXT: statement id=100 owner=0 scope=0 parent=4294967295 type=33 flags=0x3 source=test.sv:8:1 name=body
// DATABASE-NEXT: statement id=150 owner=0 scope=0 parent=1 type=38 flags=0x0 source=test.sv:8:7 name=
// DATABASE-NEXT: statement id=200 owner=0 scope=0 parent=1 type=15 flags=0x0 source=test.sv:9:3 name=
// DATABASE-NEXT: statement id=250 owner=0 scope=0 parent=3 type=38 flags=0x0 source=test.sv:9:9 name=
// DATABASE-NEXT: statement id=300 owner=6 scope=0 parent=4294967295 type=38 flags=0x0 source=test.sv:11:3 name=
// DATABASE: fixed_property source_table=0 source=0 selector=15 kind=3 value=definition.sv
// DATABASE-NEXT: fixed_property source_table=0 source=0 selector=16 kind=1 value=27
// DATABASE-NEXT: fixed_property source_table=2 source=1 selector=74 kind=0 value=true
// DATABASE-NEXT: statement_site id=1000 statement=1 phase=0 flags=0x0
// DATABASE-NEXT: statement_site id=1100 statement=3 phase=1 flags=0x0
// DATABASE-NEXT: statement_site id=1200 statement=3 phase=2 flags=0x0
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=8 ordinal=0 target_table=2 target=0
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=92 ordinal=0 target_table=0 target=1
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=92 ordinal=1 target_table=1 target=3
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=92 ordinal=2 target_table=1 target=6
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=99 ordinal=0 target_table=1 target=0
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=99 ordinal=1 target_table=1 target=1
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=99 ordinal=2 target_table=1 target=2
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=127 ordinal=0 target_table=1 target=3
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=127 ordinal=1 target_table=1 target=6
// DATABASE-NEXT: relation source_table=0 source=0 source_type=32 mode=iterate selector=601 ordinal=0 target_table=0 target=1
// DATABASE-NEXT: relation source_table=0 source=1 source_type=601 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=0 source=1 source_type=601 mode=handle selector=745 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=0 source_type=24 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=0 source_type=24 mode=handle selector=84 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=0 source_type=24 mode=handle selector=104 ordinal=0 target_table=2 target=1
// DATABASE-NEXT: relation source_table=1 source=0 source_type=24 mode=handle selector=745 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=1 source_type=1 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=1 source_type=1 mode=handle selector=84 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=1 source_type=1 mode=handle selector=745 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=2 source_type=676 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=2 source_type=676 mode=handle selector=84 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=2 source_type=676 mode=handle selector=745 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=3 source_type=20 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=3 source_type=20 mode=handle selector=84 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=6 source_type=59 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=6 source_type=59 mode=handle selector=84 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=1 source=6 source_type=59 mode=handle selector=104 ordinal=0 target_table=2 target=5
// DATABASE-NEXT: relation source_table=2 source=0 source_type=8 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=2 source=0 source_type=8 mode=handle selector=745 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=2 source=1 source_type=33 mode=handle selector=32 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=2 source=1 source_type=33 mode=handle selector=84 ordinal=0 target_table=0 target=0
// DATABASE-NEXT: relation source_table=2 source=1 source_type=33 mode=iterate selector=104 ordinal=0 target_table=2 target=2
// DATABASE-NEXT: relation source_table=2 source=1 source_type=33 mode=iterate selector=104 ordinal=1 target_table=2 target=3
// DATABASE-NEXT: relation source_table=2 source=2 source_type=38 mode=handle selector=84 ordinal=0 target_table=2 target=1
// DATABASE-NEXT: relation source_table=2 source=3 source_type=15 mode=handle selector=75 ordinal=0 target_table=2 target=4
// DATABASE-NEXT: relation source_table=2 source=3 source_type=15 mode=iterate selector=75 ordinal=0 target_table=2 target=4
// DATABASE-NEXT: relation source_table=2 source=3 source_type=15 mode=handle selector=84 ordinal=0 target_table=2 target=1
// DATABASE-NEXT: relation source_table=2 source=4 source_type=38 mode=handle selector=84 ordinal=0 target_table=2 target=1
// DATABASE-NEXT: relation source_table=2 source=5 source_type=38 mode=handle selector=84 ordinal=0 target_table=1 target=6

// WAVEFORM-NOT: statement
// WAVEFORM-NOT: relation
// WAVEFORM-NOT: fixed_property
