// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 24.7 gives every program instance its own process domain.
// A descendant's program.exit terminates its complete program, including
// sibling roots, without terminating another program. Natural completion of
// that surviving program waits for its detached descendant, then requests
// orderly finish and runs final processes before later module work.
// CHECK: exit-descendant-3
// CHECK-NEXT: survivor-root-5
// CHECK-NEXT: survivor-descendant-7
// CHECK-NEXT: final-ran
// CHECK-NOT: FAIL

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @program_exit_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9940000 in 0 root_initializer
        hierarchy "top.root"
    obelisk_sim.code_unit.decl 9940001 in 0 initial
        hierarchy "top.exiting.parent"
    obelisk_sim.code_unit.decl 9940002 in 0 initial
        hierarchy "top.exiting.sibling"
    obelisk_sim.code_unit.decl 9940003 in 0 fork
        hierarchy "top.exiting.descendant"
    obelisk_sim.code_unit.decl 9940004 in 0 initial
        hierarchy "top.survivor.root"
    obelisk_sim.code_unit.decl 9940005 in 0 initial
        hierarchy "top.survivor.parent"
    obelisk_sim.code_unit.decl 9940006 in 0 fork
        hierarchy "top.survivor.descendant"
    obelisk_sim.code_unit.decl 9940007 in 0 initial
        hierarchy "top.module_hold"
    obelisk_sim.code_unit.decl 9940008 in 0 final
        hierarchy "top.final"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %exit_parent = obelisk_sim.spawn @exit_parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %exit_sibling = obelisk_sim.spawn @exit_sibling(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %survivor_root = obelisk_sim.spawn @survivor_root(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %survivor_parent = obelisk_sim.spawn @survivor_parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %module_hold = obelisk_sim.spawn @module_hold(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %final = obelisk_sim.spawn @final_process(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @exit_parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    obelisk_sim.program_owner_id = 1001 : i64} {
      %descendant = obelisk_sim.spawn @exit_descendant(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %hold = obelisk_sim.time.constant 20
      obelisk_sim.suspend.delay %hold to ^failed
    ^failed:
      %message = obelisk_sim.bytes.constant "exit-parent-FAIL"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @exit_sibling(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940002 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    obelisk_sim.program_owner_id = 1001 : i64} {
      %hold = obelisk_sim.time.constant 10
      obelisk_sim.suspend.delay %hold to ^failed
    ^failed:
      %message = obelisk_sim.bytes.constant "exit-sibling-FAIL"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @exit_descendant(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9940003 : i64,
                    domain = 1 : i32, home_region = 10 : i32, internal,
                    obelisk_sim.detached_controls} {
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^exit
    ^exit:
      %message = obelisk_sim.bytes.constant "exit-descendant-3"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.program.exit %ctx
      obelisk_sim.return
    }

    obelisk_sim.func private @survivor_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940004 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    obelisk_sim.program_owner_id = 2002 : i64} {
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      %message = obelisk_sim.bytes.constant "survivor-root-5"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @survivor_parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940005 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    obelisk_sim.program_owner_id = 2002 : i64} {
      %descendant = obelisk_sim.spawn @survivor_descendant(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @survivor_descendant(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9940006 : i64,
                    domain = 1 : i32, home_region = 10 : i32, internal,
                    obelisk_sim.detached_controls} {
      %delay = obelisk_sim.time.constant 7
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      %message = obelisk_sim.bytes.constant "survivor-descendant-7"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @module_hold(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940007 : i64} {
      %delay = obelisk_sim.time.constant 20
      obelisk_sim.suspend.delay %delay to ^failed
    ^failed:
      %message = obelisk_sim.bytes.constant "module-hold-FAIL"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @final_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 2 : i32, code_unit_id = 9940008 : i64} {
      %message = obelisk_sim.bytes.constant "final-ran"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
