// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s --check-prefix=OUTPUT

// This deliberately omits bytecode encoding and native-state synchronization.
// A formal driver handle must therefore read and compare against the generated
// native plane, whose declaration initializer is Z.  The canonical context
// plane starts as X and is not authoritative in this configuration.
//
// LLVM-LABEL: llvm.func @read_and_rewrite_z
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_plane
// OUTPUT: NATIVE DYNAMIC INITIAL PASS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @native_dynamic_initial_state {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 :
        !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>> design
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.check"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.read_and_rewrite_z"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
      %process = obelisk_sim.spawn @check(%ctx, %driver) :
          !obelisk_sim.context,
          !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %bit = obelisk_sim.driver.subelement %driver[[1]] :
          !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %raw, %changed = obelisk_sim.call @read_and_rewrite_z(%ctx, %bit) :
          (!obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>) ->
          (!obelisk_sim.logic<1>, i1)
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %is_z = obelisk_sim.logic.compare case_eq %raw, %z :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      %false = arith.constant false
      %unchanged = arith.cmpi eq, %changed, %false : i1
      %ok = arith.andi %is_z, %unchanged : i1
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      %pass_message = obelisk_sim.bytes.constant "NATIVE DYNAMIC INITIAL PASS"
      %pass_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %pass_stdout(%pass_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^fail:
      %fail_message = obelisk_sim.bytes.constant "NATIVE DYNAMIC INITIAL FAIL"
      %fail_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @read_and_rewrite_z(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        -> (!obelisk_sim.logic<1>, i1)
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64,
                    passthrough = ["noinline"]} {
      %raw = obelisk_sim.driver.read %driver :
          !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %changed = obelisk_sim.driver.drive_changed %driver = %z {
        obelisk_sim.defer_net_resolution
      } : !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return %raw, %changed : !obelisk_sim.logic<1>, i1
    }
  }
}
