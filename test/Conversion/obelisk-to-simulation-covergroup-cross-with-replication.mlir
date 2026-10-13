// RUN: obelisk --std=1800-2023 -emit-obelisk %S/../Coverage/functional-cross-with-replication.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.lowered.mlir
// RUN: obelisk-opt %t.lowered.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.lowered.mlir

// A multiplier consumes no operand records: each replication has one payload
// edge. Zero-size replications remain reachable within their concatenation.
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=1 {{.*}} kind=37 width=130 signedness=1
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=1 {{.*}} kind=37 width=0 signedness=1
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=1 {{.*}} kind=37 width=6 signedness=1
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=1 {{.*}} kind=37 width=10 signedness=1
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
