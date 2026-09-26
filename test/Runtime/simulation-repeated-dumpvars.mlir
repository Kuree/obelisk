// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @repeated_dumpvars {
    obelisk_sim.scope.decl 0 hierarchy "repeated_dumpvars"
    obelisk_sim.code_unit.decl 9971000 in 0 root_initializer
        hierarchy "repeated_dumpvars.root"
    obelisk_sim.code_unit.decl 9971001 in 0 initial
        hierarchy "repeated_dumpvars.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9971000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9971001 : i64} {
      %scale = arith.constant -9 : i32
      %levels = arith.constant 0 : i64
      %path = obelisk_sim.bytes.constant "/dev/null"
      %scope = obelisk_sim.bytes.constant ""
      obelisk_sim.dump.timescale %ctx, %scale :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.dump.open %ctx, %path :
          (!obelisk_sim.context, !obelisk_sim.bytes) -> ()
      obelisk_sim.dump.vars %ctx, %levels, %scope :
          (!obelisk_sim.context, i64, !obelisk_sim.bytes) -> ()
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      %same_scale = arith.constant -9 : i32
      %same_levels = arith.constant 0 : i64
      %same_scope = obelisk_sim.bytes.constant ""
      obelisk_sim.dump.timescale %ctx, %same_scale :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.dump.vars %ctx, %same_levels, %same_scope :
          (!obelisk_sim.context, i64, !obelisk_sim.bytes) -> ()
      %passed = obelisk_sim.bytes.constant "PASSED"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%passed)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
