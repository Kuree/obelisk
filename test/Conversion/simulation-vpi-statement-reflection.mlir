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
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 10 in 0 initial hierarchy "top.initial"

    // Deliberately reverse declaration and site order. The wire inventory is
    // normalized by stable ID, not by mutable MLIR block order or source loc.
    obelisk_sim.statement.decl 200 in 10 scope 0 type 15 parent 100 loc("test.sv":9:3)
    obelisk_sim.statement.decl 100 in 10 scope 0 type 33 name "body" {is_protected} loc("test.sv":8:1)
    // Non-cbStmt statement kinds are still traversal-visible identities and
    // correctly have no semantic callback-site record.
    obelisk_sim.statement.decl 150 in 10 scope 0 type 38 parent 100 loc("test.sv":8:7)
    obelisk_sim.statement_site.decl 1200 on 200 phase 2
    obelisk_sim.statement_site.decl 1000 on 100 phase 0
    obelisk_sim.statement_site.decl 1100 on 200 phase 1

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

// DATABASE: statement id=100 owner=0 scope=0 parent=4294967295 type=33 flags=0x1 source=test.sv:8:1 name=body
// DATABASE-NEXT: statement id=150 owner=0 scope=0 parent=0 type=38 flags=0x0 source=test.sv:8:7 name=
// DATABASE-NEXT: statement id=200 owner=0 scope=0 parent=0 type=15 flags=0x0 source=test.sv:9:3 name=
// DATABASE-NEXT: statement_site id=1000 statement=0 phase=0 flags=0x0
// DATABASE-NEXT: statement_site id=1100 statement=2 phase=1 flags=0x0
// DATABASE-NEXT: statement_site id=1200 statement=2 phase=2 flags=0x0

// WAVEFORM-NOT: statement
