// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
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
  simulation.design @program_exit_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9940000 in 0 root_initializer
        hierarchy "top.root"
    simulation.code_unit.decl 9940001 in 0 initial
        hierarchy "top.exiting.parent"
    simulation.code_unit.decl 9940002 in 0 initial
        hierarchy "top.exiting.sibling"
    simulation.code_unit.decl 9940003 in 0 fork
        hierarchy "top.exiting.descendant"
    simulation.code_unit.decl 9940004 in 0 initial
        hierarchy "top.survivor.root"
    simulation.code_unit.decl 9940005 in 0 initial
        hierarchy "top.survivor.parent"
    simulation.code_unit.decl 9940006 in 0 fork
        hierarchy "top.survivor.descendant"
    simulation.code_unit.decl 9940007 in 0 initial
        hierarchy "top.module_hold"
    simulation.code_unit.decl 9940008 in 0 final
        hierarchy "top.final"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %exit_parent = simulation.spawn @exit_parent(%ctx) :
          !simulation.context -> !simulation.process
      %exit_sibling = simulation.spawn @exit_sibling(%ctx) :
          !simulation.context -> !simulation.process
      %survivor_root = simulation.spawn @survivor_root(%ctx) :
          !simulation.context -> !simulation.process
      %survivor_parent = simulation.spawn @survivor_parent(%ctx) :
          !simulation.context -> !simulation.process
      %module_hold = simulation.spawn @module_hold(%ctx) :
          !simulation.context -> !simulation.process
      %final = simulation.spawn @final_process(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @exit_parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    schedule.program_owner_id = 1001 : i64} {
      %descendant = simulation.spawn @exit_descendant(%ctx) :
          !simulation.context -> !simulation.process
      %hold = simulation.time.constant 20
      simulation.suspend.delay %hold to ^failed
    ^failed:
      %message = simulation.bytes.constant "exit-parent-FAIL"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @exit_sibling(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940002 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    schedule.program_owner_id = 1001 : i64} {
      %hold = simulation.time.constant 10
      simulation.suspend.delay %hold to ^failed
    ^failed:
      %message = simulation.bytes.constant "exit-sibling-FAIL"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @exit_descendant(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9940003 : i64,
                    domain = 1 : i32, home_region = 10 : i32, internal,
                    schedule.detached_controls} {
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^exit
    ^exit:
      %message = simulation.bytes.constant "exit-descendant-3"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.program.exit %ctx
      simulation.return
    }

    simulation.func private @survivor_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940004 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    schedule.program_owner_id = 2002 : i64} {
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^done
    ^done:
      %message = simulation.bytes.constant "survivor-root-5"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @survivor_parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940005 : i64,
                    domain = 1 : i32, home_region = 10 : i32,
                    schedule.program_owner_id = 2002 : i64} {
      %descendant = simulation.spawn @survivor_descendant(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @survivor_descendant(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9940006 : i64,
                    domain = 1 : i32, home_region = 10 : i32, internal,
                    schedule.detached_controls} {
      %delay = simulation.time.constant 7
      simulation.suspend.delay %delay to ^done
    ^done:
      %message = simulation.bytes.constant "survivor-descendant-7"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @module_hold(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940007 : i64} {
      %delay = simulation.time.constant 20
      simulation.suspend.delay %delay to ^failed
    ^failed:
      %message = simulation.bytes.constant "module-hold-FAIL"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @final_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 2 : i32, code_unit_id = 9940008 : i64} {
      %message = simulation.bytes.constant "final-ran"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
