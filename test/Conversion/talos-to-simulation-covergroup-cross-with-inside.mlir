// RUN: talos --std=1800-2023 -emit-talos %S/../Coverage/functional-cross-with-inside.sv | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.lowered.mlir
// RUN: talos-opt %t.lowered.mlir --test-talos-print-coverage-schema -o /dev/null > %t.schema 2>&1
// RUN: FileCheck %s --check-prefix=SCHEMA < %t.schema
// RUN: FileCheck %s --check-prefix=SHARED < %t.schema
// RUN: FileCheck %s --check-prefix=SIM < %t.lowered.mlir

// Membership is a shared Boolean DAG of comparisons. Bounded, unbounded,
// and integer absolute-tolerance clauses use their actual operand arity.
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=2 {{.*}} kind=38 width=1 signedness=1
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=2 {{.*}} kind=39 width=1 signedness=1
// SCHEMA-DAG: cross_predicate {{.*}} kind=40 width=1 signedness=1
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=3 {{.*}} kind=41 width=1 signedness=1 {{.*}} flags=0
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=2 {{.*}} kind=41 width=1 signedness=1 {{.*}} flags=8
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=2 {{.*}} kind=41 width=1 signedness=1 {{.*}} flags=16
// SCHEMA-DAG: cross_predicate {{.*}} operand_count=3 {{.*}} kind=41 width=1 signedness=1 {{.*}} flags=32
// One searched-value node is shared by every clause of an inside node. This
// is a separate FileCheck run: in one DAG group, a kind census line above
// could claim a clause line first, depending on the path-derived ID order.
// SHARED-DAG: cross_predicate id={{[0-9]+}} selector={{[0-9]+}} first_operand={{[0-9]+}} operand_count=2 operands=[[CLAUSE0:[0-9]+]],[[CLAUSE1:[0-9]+]] {{.*}}kind=40
// SHARED-DAG: cross_predicate id=[[CLAUSE0]] {{.*}}operands=[[SEARCHED:[0-9]+]],
// SHARED-DAG: cross_predicate id=[[CLAUSE1]] {{.*}}operands=[[SEARCHED]],
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
