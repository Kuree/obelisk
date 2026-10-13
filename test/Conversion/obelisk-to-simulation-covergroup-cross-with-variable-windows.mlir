// RUN: %obelisk --std=1800-2023 -emit-obelisk %S/../Coverage/functional-cross-with-variable-windows.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir

// Dynamic bases retain their typed target operand and signed width. Packed
// element widths and declared ascending ranges remain in the predicate DAG;
// no constructor tuple queue is generated for any of these windows.
// SCHEMA-DAG: cross_predicate {{.*}} selector=[[OWNER:[1-9][0-9]*]] left={{[1-9][0-9]*}} right=[[INDEX:[1-9][0-9]*]] {{.*}} kind=23 width=2 {{.*}} flags=6 element_width=1 index_base=64
// SCHEMA-DAG: cross_predicate id=[[INDEX]] selector=[[OWNER]] {{.*}} kind=2 width=8 signedness=2
// SCHEMA-DAG: cross_predicate {{.*}} kind=23 width=2 {{.*}} flags=0 element_width=1 index_base=0
// SCHEMA-DAG: cross_predicate {{.*}} kind=23 width=2 {{.*}} flags=4 element_width=1 index_base=0
// SCHEMA-DAG: cross_predicate {{.*}} kind=23 width=2 {{.*}} flags=2 element_width=1 index_base=64
// SCHEMA-DAG: cross_predicate {{.*}} kind=23 width=4 {{.*}} element_width=2 index_base=0
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
