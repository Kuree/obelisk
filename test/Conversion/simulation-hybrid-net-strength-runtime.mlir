// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true})' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  simulation.design @hybrid_net_strength {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.value"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 6 : i32, strength1 = 6 : i32
    }
    simulation.code_unit.decl 9970000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9970001 in 0 continuous hierarchy "top.drive"
    simulation.code_unit.decl 9970002 in 0 fork hierarchy "top.report" {internal}

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %process = simulation.spawn @drive(%ctx, %driver, %net) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      %monitor = simulation.spawn @report(%ctx, %net) :
          !simulation.context, !simulation.net<!simulation.logic<1>> ->
          !simulation.process
      simulation.monitor.register %monitor
      simulation.return
    }

    simulation.func private @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9970001 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %driver = %one :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }

    simulation.func private @report(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 13 : i32, code_unit_id = 9970002 : i64,
                    home_region = 16 : i32, schedule.detached_controls,
                    internal} {
      cf.br ^check
    ^check:
      %current = simulation.monitor.current
      cf.cond_br %current, ^display, ^done
    ^display:
      %value = simulation.net.read %net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %stdout = arith.constant 1 : i32
      %format = simulation.bytes.constant "strength %v"
      simulation.display %ctx to %stdout(%format, %value, %net)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      simulation.suspend.change %net to ^check {resume_region = 16 : i32} :
          !simulation.net<!simulation.logic<1>>
    ^done:
      simulation.return
    }
  }
}
