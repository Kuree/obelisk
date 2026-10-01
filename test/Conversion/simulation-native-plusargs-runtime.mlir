// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAIN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=RESUMED < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=SCALAR < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-native-plusargs-runtime.test.

// Block-local plusarg strings are native-AOT eligible but still need a live
// managed lane. Exercise both a plain startup actor and a resumed coroutine;
// the root itself never creates a string that could accidentally supply one.
// Fixed unpacked array references, packed driver handles, and named-block
// tokens must not acquire a managed scope, unlike the plusarg strings below.
// PLAN: __obelisk_aot_schedule_plan_v1
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot
// PLAIN-LABEL: llvm.mlir.global external constant @initial.__obelisk_process_descriptor()
// PLAIN-NOT: llvm.insertvalue {{.*}}[2] : !llvm.struct<(struct
// PLAIN: llvm.return
// RESUMED-LABEL: llvm.mlir.global external constant @resumed.__obelisk_process_descriptor()
// RESUMED-NOT: llvm.insertvalue {{.*}}[2] : !llvm.struct<(struct
// RESUMED: llvm.return
// SCALAR-LABEL: llvm.mlir.global external constant @scalar.__obelisk_process_descriptor()
// SCALAR: llvm.insertvalue {{.*}}[1] : !llvm.struct<(struct
// SCALAR: %[[FLAG:.*]] = llvm.mlir.constant(1 : i32)
// SCALAR-NEXT: llvm.insertvalue %[[FLAG]], {{.*}}[2] : !llvm.struct<(struct
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  simulation.design @plusargs {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<3 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 3 x !simulation.packed_array<3 : 0 x !simulation.logic<1>>> design
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "initial"
    simulation.code_unit.decl 3 in 0 initial hierarchy "resumed"
    simulation.code_unit.decl 4 in 0 initial hierarchy "scalar"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.spawn @initial(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @resumed(%ctx) : !simulation.context -> !simulation.process
      %dst = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>>
      %c = simulation.spawn @scalar(%ctx, %dst) : !simulation.context, !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>> -> !simulation.process
      simulation.return
    }
    simulation.func @scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dst: !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %scope = simulation.control.enter 42
      %bits = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      %value = simulation.packed.unflatten %bits : (!simulation.logic<4>) -> !simulation.packed_array<3 : 0 x !simulation.logic<1>>
      %array = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.packed_array<3 : 0 x !simulation.logic<1>>>>
      %element = simulation.ref.subelement %array[[2]] : !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.packed_array<3 : 0 x !simulation.logic<1>>>> -> !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>>
      simulation.ref.store %value to %element : !simulation.packed_array<3 : 0 x !simulation.logic<1>>, !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>>
      %observed = simulation.ref.load %element : !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<3 : 0 x !simulation.logic<1>>
      simulation.ref.store %observed to %dst : !simulation.packed_array<3 : 0 x !simulation.logic<1>>, !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<4>>
      simulation.driver.drive %driver = %bits : !simulation.driver<!simulation.logic<4>>, !simulation.logic<4>
      simulation.control.leave %scope
      simulation.return
    }
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %prefix = simulation.string.literal "n="
      %tail, %found = simulation.plusarg.value %ctx, %prefix : (!simulation.context, !simulation.string) -> (!simulation.string, i32)
      %parsed = simulation.plusarg.parse_logic %tail {radix = #simulation.radix<decimal>} : (!simulation.string) -> !simulation.logic<32>
      %zero = arith.constant 0 : i32
      %matched = arith.cmpi ne, %found, %zero : i32
      %default = simulation.logic.constant 99 : i32, 0 : i32 : !simulation.logic<32>
      %value = arith.select %matched, %parsed, %default : !simulation.logic<32>
      %format = simulation.bytes.constant "initial found=%0d value=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%format, %found, %value) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, i32, !simulation.logic<32>
      simulation.return
    }
    simulation.func @resumed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %prefix = simulation.string.literal "n="
      %tail, %found = simulation.plusarg.value %ctx, %prefix : (!simulation.context, !simulation.string) -> (!simulation.string, i32)
      %parsed = simulation.plusarg.parse_logic %tail {radix = #simulation.radix<decimal>} : (!simulation.string) -> !simulation.logic<32>
      %zero = arith.constant 0 : i32
      %matched = arith.cmpi ne, %found, %zero : i32
      %default = simulation.logic.constant 99 : i32, 0 : i32 : !simulation.logic<32>
      %value = arith.select %matched, %parsed, %default : !simulation.logic<32>
      %format = simulation.bytes.constant "resumed found=%0d value=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%format, %found, %value) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, i32, !simulation.logic<32>
      simulation.return
    }
  }
}
