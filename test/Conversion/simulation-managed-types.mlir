// RUN: obelisk-opt %s | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @managed_types {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.string design
        hierarchy "top.text"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>> design
        hierarchy "top.values"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.queue<!obelisk_sim.string, 8> design
        hierarchy "top.names"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.assoc_array<!obelisk_sim.string, i64, false> design
        hierarchy "top.lookup"
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %empty = obelisk_sim.managed.null :
        !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
      obelisk_sim.return
    }
  }
}

// CHECK: !obelisk_sim.string
// CHECK: !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
// CHECK: !obelisk_sim.queue<!obelisk_sim.string, 8>
// CHECK: !obelisk_sim.assoc_array<!obelisk_sim.string, i64, false>
// CHECK: obelisk_sim.managed.null

// A container whose element is logic remains one managed register. It must
// not acquire a second unknown plane from recursive containsLogic analysis.
// BYTECODE: obelisk.execution.state_bits = 256 : i64
// BYTECODE: obelisk_sim.storage.decl 0
// BYTECODE: obelisk_sim.storage.decl 1
// BYTECODE: obelisk_sim.storage.decl 2
// BYTECODE: obelisk_sim.storage.decl 3
