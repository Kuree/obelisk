// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true})' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=NATIVE

// IEEE 1800-2017 21.2.1.5 and Clause 28: a %v conversion observes the
// resolved strength of its scalar net. Preserve the bytecode monitor/display
// path, including the net handle paired with the displayed value, while the
// continuous driver remains available to the native schedule.
//
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010215
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010217
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010002 inputs=5 outputs=0
// NATIVE: obelisk.execution.flags
// NATIVE: llvm.mlir.global internal constant @report.__obelisk_bytecode_entry

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @hybrid_net_strength {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.value"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 6 : i32, strength1 = 6 : i32
    }
    obelisk_sim.code_unit.decl 9970000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9970001 in 0 continuous hierarchy "top.drive"
    obelisk_sim.code_unit.decl 9970002 in 0 fork hierarchy "top.report" {internal}

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @drive(%ctx, %driver, %net) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %monitor = obelisk_sim.spawn @report(%ctx, %net) :
          !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      obelisk_sim.monitor.register %monitor
      obelisk_sim.return
    }

    obelisk_sim.func private @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9970001 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func private @report(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 13 : i32, code_unit_id = 9970002 : i64,
                    home_region = 16 : i32, obelisk_sim.detached_controls,
                    internal} {
      cf.br ^check
    ^check:
      %current = obelisk_sim.monitor.current
      cf.cond_br %current, ^display, ^done
    ^display:
      %value = obelisk_sim.net.read %net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %stdout = arith.constant 1 : i32
      %format = obelisk_sim.bytes.constant "strength %v"
      obelisk_sim.display %ctx to %stdout(%format, %value, %net)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.suspend.change %net to ^check {resume_region = 16 : i32} :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
    ^done:
      obelisk_sim.return
    }
  }
}
