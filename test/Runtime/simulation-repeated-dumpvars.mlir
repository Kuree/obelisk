// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A later $dumpvars is ignored after the VCD plan and header are complete.
// Lowering also attaches the same design timescale to each call; that repeated
// annotation is idempotent and must not terminate simulation.
// CHECK: PASSED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @repeated_dumpvars {
    simulation.scope.decl 0 hierarchy "repeated_dumpvars"
    simulation.code_unit.decl 9971000 in 0 root_initializer
        hierarchy "repeated_dumpvars.root"
    simulation.code_unit.decl 9971001 in 0 initial
        hierarchy "repeated_dumpvars.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9971000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9971001 : i64} {
      %scale = arith.constant -9 : i32
      %levels = arith.constant 0 : i64
      %path = simulation.bytes.constant "/dev/null"
      %scope = simulation.bytes.constant ""
      simulation.dump.timescale %ctx, %scale :
          (!simulation.context, i32) -> ()
      simulation.dump.open %ctx, %path :
          (!simulation.context, !simulation.bytes) -> ()
      simulation.dump.vars %ctx, %levels, %scope :
          (!simulation.context, i64, !simulation.bytes) -> ()
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %same_scale = arith.constant -9 : i32
      %same_levels = arith.constant 0 : i64
      %same_scope = simulation.bytes.constant ""
      simulation.dump.timescale %ctx, %same_scale :
          (!simulation.context, i32) -> ()
      simulation.dump.vars %ctx, %same_levels, %same_scope :
          (!simulation.context, i64, !simulation.bytes) -> ()
      %passed = simulation.bytes.constant "PASSED"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%passed)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
