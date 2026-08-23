// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 19.11.1: a coverpoint with no nonempty bins contributes
// neither to the covergroup average nor to the reported bin totals.
// CHECK: mixed 100 1 1
// CHECK: empty 0 0 0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @coverage_empty_bins {
    obelisk_sim.scope.decl 0 hierarchy "coverage_empty_bins"
    obelisk_sim.covergroup.decl @mixed id 1 bins [0, 1]
    obelisk_sim.covergroup.decl @empty id 2 bins [0]
    obelisk_sim.code_unit.decl 9910100 in 0 root_initializer
        hierarchy "coverage_empty_bins.root"
    obelisk_sim.code_unit.decl 9910101 in 0 initial
        hierarchy "coverage_empty_bins.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910100 : i64} {
      %initial = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910101 : i64} {
      %mixed = obelisk_sim.covergroup.create %ctx from @mixed
          : !obelisk_sim.context -> !obelisk_sim.covergroup_handle<@mixed>
      %hit = arith.constant true
      obelisk_sim.covergroup.sample %ctx, %mixed[%hit]
          : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@mixed>
      %mixed_percentage, %mixed_covered, %mixed_total =
          obelisk_sim.covergroup.instance_query %ctx, %mixed
          : (!obelisk_sim.context,
             !obelisk_sim.covergroup_handle<@mixed>) -> (f64, i32, i32)
      %mixed_format = obelisk_sim.bytes.constant "mixed %.0f %d %d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %mixed_format, %mixed_percentage, %mixed_covered, %mixed_total)
          newline = true radix = 10 flags = [0, 4, 0, 0]
          : !obelisk_sim.bytes, f64, i32, i32

      %empty = obelisk_sim.covergroup.create %ctx from @empty
          : !obelisk_sim.context -> !obelisk_sim.covergroup_handle<@empty>
      obelisk_sim.covergroup.sample %ctx, %empty[]
          : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@empty>
      %empty_percentage, %empty_covered, %empty_total =
          obelisk_sim.covergroup.instance_query %ctx, %empty
          : (!obelisk_sim.context,
             !obelisk_sim.covergroup_handle<@empty>) -> (f64, i32, i32)
      %empty_format = obelisk_sim.bytes.constant "empty %.0f %d %d"
      obelisk_sim.display %ctx to %stdout(
          %empty_format, %empty_percentage, %empty_covered, %empty_total)
          newline = true radix = 10 flags = [0, 4, 0, 0]
          : !obelisk_sim.bytes, f64, i32, i32
      obelisk_sim.return
    }
  }
}
