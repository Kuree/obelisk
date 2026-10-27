// RUN: talos --std=1800-2023 --coverage=functional -emit-talos %S/../Coverage/functional-bin-group-packing.sv -o %t.input.mlir
// RUN: talos-opt %t.input.mlir '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.lowered.mlir
// RUN: talos-opt %t.lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: talos-opt %t.lowered.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.lowered.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.lowered.mlir --encode-talos-sim-to-bytecode='vpi=off' --test-talos-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// Both lowerings retain constructor-resolved bin groups in the shared schema.
// Automatic integral bins remain runtime work; signed sample types, default
// arrays, empty with-filtered arrays, and their owners survive MLIR passes.
// SIM: simulation.covergroup.decl
// SIM: simulation.covergroup.create
// SIM: simulation.covergroup.sample_enabled
// SIM: simulation.covergroup.sample

// BYTECODE-DAG: intrinsic {{.*}}id=0x00010450 inputs={{[0-9]+}} outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010456 inputs={{[0-9]+}} outputs=0 flags=0
// SIM: simulation.covergroup.sample_enabled
// SIM: simulation.covergroup.sample

// SCHEMA-DAG: functional_item id={{[0-9]+}} type={{[0-9]+}} name=zero kind=1 ordinal=0
// SCHEMA-DAG: functional_item id={{[0-9]+}} type={{[0-9]+}} name=first kind=1 ordinal=1
// SCHEMA-DAG: functional_item id={{[0-9]+}} type={{[0-9]+}} name=defaults kind=1 ordinal=2
// SCHEMA-DAG: functional_item id={{[0-9]+}} type={{[0-9]+}} name=empty_default kind=1 ordinal=3
// SCHEMA-DAG: functional_item id={{[0-9]+}} type={{[0-9]+}} name=last kind=1 ordinal=4
// SCHEMA-DAG: functional_bin id=[[DEFAULT:[0-9]+]] item={{[0-9]+}} name=other kind=1 flags=1 ordinal=0 at_least=1 hierarchy=top.cg.defaults.other
// SCHEMA-DAG: functional_bin id=[[DUPLICATE:[0-9]+]] item={{[0-9]+}} name=duplicate kind=1 flags=1 ordinal=1
// SCHEMA-DAG: functional_bin id=[[FILTERED:[0-9]+]] item={{[0-9]+}} name=none kind=1 flags=0 ordinal=2
// SCHEMA-DAG: functional_bin id=[[EMPTY:[0-9]+]] item={{[0-9]+}} name=other kind=1 flags=1 ordinal=0 at_least=1 hierarchy=top.cg.empty_default.other
// SCHEMA-DAG: functional_expression id={{[0-9]+}} owner={{[0-9]+}} owner_kind=2 role=2 result_kind=2 width=3 signedness=2
// SCHEMA-DAG: functional_bin_plan bin=[[DEFAULT]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2
// SCHEMA-DAG: functional_bin_plan bin=[[DUPLICATE]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2
// SCHEMA-DAG: functional_bin_plan bin=[[EMPTY]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2
// SCHEMA-DAG: functional_bin_plan bin=[[FILTERED]] value_set={{[1-9][0-9]*}} iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2
// SCHEMA-DAG: functional_option_plan owner={{[0-9]+}} expression={{[0-9]+}} owner_kind=1 scope=1 option=4 ordinal=4
// SCHEMA-DAG: functional_option_plan owner={{[0-9]+}} expression={{[0-9]+}} owner_kind=2 scope=1 option=4 ordinal=4
