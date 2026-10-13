// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s --check-prefix=ROUNDTRIP
// RUN: %coverage-fixture %t.obcov 1 preparation 0 construction-with
// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=bytes-attr=obelisk.execution.coverage_schema_blob bytes-file=%t.obcov' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=bytes-attr=obelisk.execution.coverage_schema_blob bytes-file=%t.obcov' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: not obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk-opt %s '--test-obelisk-set-module-attributes=bytes-attr=obelisk.execution.coverage_schema_blob bytes-file=%t.obcov' \
// RUN:   '--test-obelisk-edit-attributes=op=simulation.covergroup.prepare set={expression_ids = array<i64: 101, 100>}' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines 2>&1 | FileCheck %s --check-prefix=BAD-BATCH
// RUN: not obelisk-opt %s '--test-obelisk-set-module-attributes=bytes-attr=obelisk.execution.coverage_schema_blob bytes-file=%t.obcov' \
// RUN:   '--test-obelisk-edit-attributes=op=simulation.covergroup.candidates match={selector = 71 : i64} set={selector = 999 : i64}' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 | FileCheck %s --check-prefix=BAD-SELECTOR

!tuple = !simulation.unpacked_struct<[
  #simulation.field<name = "point", type = i65, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "side", type = i1, ordinal = 1, packedOffset = 0>
]>
!queue = !simulation.queue<!tuple, 0>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @preparation {
    simulation.scope.decl 0 hierarchy "top"
    simulation.covergroup.decl @cg schema 10
    simulation.code_unit.decl 1 in 0 function hierarchy "top.select"
    simulation.func @select(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %queue: !queue {simulation.capture_kind = 1 : i32},
        %nested: !queue {simulation.capture_kind = 1 : i32},
        %lower: i65 {simulation.capture_kind = 1 : i32},
        %upper: i65 {simulation.capture_kind = 1 : i32}) -> i64
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %token = simulation.covergroup.prepare %ctx from @cg
        payloads [%lower, %upper] expression_ids [100, 101]
        : (i65, i65) -> i64
      simulation.covergroup.candidates %ctx, %token into %queue
        selector 71 maximum 8 : !queue
      simulation.covergroup.bind_with %ctx, %token from %queue selector 71 : !queue
      simulation.covergroup.candidates %ctx, %token into %nested
        selector 72 maximum 8 : !queue
      simulation.covergroup.bind_with %ctx, %token from %nested selector 72 : !queue
      simulation.covergroup.release_preparation %ctx, %token
      %size = simulation.container.size %queue : (!queue) -> i64
      simulation.return %size : i64
    }
  }
}

// ROUNDTRIP: simulation.covergroup.prepare
// ROUNDTRIP: simulation.covergroup.candidates
// ROUNDTRIP: simulation.covergroup.bind_with
// ROUNDTRIP: simulation.covergroup.release_preparation
// NATIVE-LABEL: llvm.func @select
// NATIVE: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_prepare
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_candidates
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_bind_with
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_release_preparation
// NATIVE: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop
// NATIVE-NOT: simulation.covergroup.
// BYTECODE-DAG: intrinsic {{[0-9]+}}: id=0x0001047e {{.*}}outputs=1
// BYTECODE-DAG: intrinsic {{[0-9]+}}: id=0x0001047f {{.*}}outputs=0
// BYTECODE-DAG: intrinsic {{[0-9]+}}: id=0x00010481 {{.*}}outputs=0
// BYTECODE-DAG: intrinsic {{[0-9]+}}: id=0x00010480 {{.*}}outputs=0
// MISSING: typed functional coverage requires an embedded exact-v1 coverage schema
// BAD-BATCH: does not contain the complete schema-ordered primitive constructor FunctionalExpression batch
// BAD-SELECTOR: has a nonexistent cross selector ID
