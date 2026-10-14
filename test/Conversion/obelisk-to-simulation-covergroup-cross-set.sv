// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --obelisk-sim-prepare | FileCheck %s --check-prefix=PREPARE
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: not %obelisk --std=1800-2023 -emit-obelisk %S/Inputs/cross-set-wrong-type.sv 2>&1 | FileCheck %s --check-prefix=WRONG-TYPE
// RUN: %obelisk --std=1800-2023 -emit-obelisk %S/Inputs/cross-set-zero-matches.sv | not obelisk-opt --obelisk-sim-prepare 2>&1 | FileCheck %s --check-prefix=ZERO-MATCHES

// IEEE 1800-2023 19.6.1.3-.4 gives every cross an implicit unbounded
// CrossQueueType whose unpacked CrossValType fields exactly follow target
// declaration order and effective types. Preparation retains that semantic
// type on each queue expression; no finite-domain expansion is involved.
// PREPARE: obelisk.sv.bins.set_expr
// PREPARE: semantic_type = !obelisk.queue<!obelisk.source_aggregate<{{.*}}, 0>

// CrossVal transport is described solely by the target-independent simulation
// provenance layout. The first target retains signedness while both fields
// retain the coverpoints' effective four-state types.
// SCHEMA-DAG: cross_plan item=[[CROSS:[1-9][0-9]*]] {{.*}} tuple_element_type={{[1-9][0-9]*}} tuple_provenance_span={{[1-9][0-9]*}} tuple_flags=1
// SCHEMA-DAG: cross_target cross=[[CROSS]] target={{[1-9][0-9]*}} ordinal=0 tuple_bit_offset={{[0-9]+}} tuple_bit_width=2 tuple_result_kind=2 tuple_signedness=2 tuple_flags=1
// SCHEMA-DAG: cross_target cross=[[CROSS]] target={{[1-9][0-9]*}} ordinal=1 tuple_bit_offset={{[0-9]+}} tuple_bit_width=1 tuple_result_kind=2 tuple_signedness=1 tuple_flags=3

// A set selector owns one constructor-phase BinSet expression with the
// TupleQueue result kind. Omitted matches is Count 1, ordinary large counts
// are preserved, `$` is All in 1800-2017, dynamic counts remain typed
// constructor expressions, and wider-than-u64 positives retain their typed
// constructor values.
// SCHEMA-DAG: cross_selector id=[[OMITTED:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=5 {{.*}} construction_expression=[[OMITTED_SET:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=1
// SCHEMA-DAG: cross_selector id=[[COUNT:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=5 {{.*}} construction_expression=[[COUNT_SET:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=5000
// SCHEMA-DAG: cross_selector id=[[DYNAMIC:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=5 {{.*}} construction_expression=[[DYNAMIC_SET:[1-9][0-9]*]] {{.*}} matches_expression=[[MATCHES:[1-9][0-9]*]] matches_policy=2 matches_count=0
// SCHEMA-DAG: cross_selector id=[[ALL:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=5 {{.*}} construction_expression=[[ALL_SET:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=3 matches_count=0
// SCHEMA-DAG: cross_selector id=[[HUGE:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=5 {{.*}} construction_expression=[[HUGE_SET:[1-9][0-9]*]] {{.*}} matches_expression=[[HUGE_MATCHES:[1-9][0-9]*]] matches_policy=2 matches_count=0
// SCHEMA-DAG: cross_selector id=[[FILTER_SET:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=5 {{.*}} construction_expression=[[FILTER_QUEUE:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=2
// SCHEMA-DAG: cross_selector id=[[FILTER:[1-9][0-9]*]] cross=[[CROSS]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=0 matches_policy=2 matches_count=2 flags=0 predicate_root=[[FILTER_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: functional_expression id=[[OMITTED_SET]] owner=[[OMITTED]] owner_kind=4 role=6 result_kind=7 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[COUNT_SET]] owner=[[COUNT]] owner_kind=4 role=6 result_kind=7 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[DYNAMIC_SET]] owner=[[DYNAMIC]] owner_kind=4 role=6 result_kind=7 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[MATCHES]] owner=[[DYNAMIC]] owner_kind=4 role=12 result_kind=2 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[ALL_SET]] owner=[[ALL]] owner_kind=4 role=6 result_kind=7 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[HUGE_SET]] owner=[[HUGE]] owner_kind=4 role=6 result_kind=7 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[HUGE_MATCHES]] owner=[[HUGE]] owner_kind=4 role=12 result_kind=2 width=128 {{.*}} phase=1
// SCHEMA-DAG: functional_expression id=[[FILTER_QUEUE]] owner=[[FILTER_SET]] owner_kind=4 role=6 result_kind=7 {{.*}} phase=1
// SCHEMA-DAG: cross_predicate id=[[FILTER_ROOT]] selector=[[FILTER]] {{.*}} kind=4 width=1

// The queue-valued constructor expressions are ordinary covergroup-create
// payloads. Native lowering passes each managed word through the descriptor's
// owner field, and bytecode encoding accepts the same managed register shape.
// SIM: simulation.covergroup.create
// SIM: !simulation.queue<!simulation.unpacked_struct<{{.*}}>, 0>
// NATIVE: %[[OWNER_SLOT:.*]] = llvm.getelementptr %{{.*}}[40] : (!llvm.ptr) -> !llvm.ptr, i8
// NATIVE-NEXT: llvm.store %{{.*}}, %[[OWNER_SLOT]] {{.*}} : !llvm.ptr, !llvm.ptr
// NATIVE: %[[QUEUE_KIND:.*]] = llvm.mlir.constant(6 : i32)
// NATIVE: llvm.store %[[QUEUE_KIND]]
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_create

// WRONG-TYPE: no implicit conversion from 'WrongVal$[$]' to 'CrossQueueType'
// ZERO-MATCHES: cross_set_expression matches constant must be a positive v1 integer

module cross_set;
  bit signed [1:0] a;
  logic b;

  covergroup cg(int need);
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins omitted = '{'{-1, 1}, '{0, 0}};
      bins count = '{'{1, 1}} matches 5000;
      bins dynamic = '{'{0, 1}, '{1, 0}} matches need;
      bins all = '{'{-2, 0}} matches $;
      bins huge = '{'{1, 0}}
          matches 128'h1_0000_0000_0000_0000;
      bins filtered = ('{'{0, 0}, '{1, 1}} matches 2)
          with (ca == cb) matches 2;
    }
  endgroup

  cg cov = new(2);
endmodule

