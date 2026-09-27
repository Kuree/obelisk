// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=FEATURE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @bitstream {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.cast"
    simulation.func @cast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.dynamic_array<!simulation.logic<8>>
            {simulation.capture_kind = 1 : i32},
        %queue: !simulation.queue<!simulation.logic<8>, 0>
            {simulation.capture_kind = 1 : i32},
        %bits: !simulation.dynamic_array<i8>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %result = simulation.container.export_bitstream %source :
          (!simulation.dynamic_array<!simulation.logic<8>>) ->
          !simulation.logic<24>
      %two_state = simulation.container.export_bitstream %queue :
          (!simulation.queue<!simulation.logic<8>, 0>) -> i24
      %four_state = simulation.container.export_bitstream %bits :
          (!simulation.dynamic_array<i8>) -> !simulation.logic<24>
      %wide = simulation.container.export_bitstream %source :
          (!simulation.dynamic_array<!simulation.logic<8>>) ->
          !simulation.logic<65536>
      simulation.return
    }
  }
}

// NATIVE-COUNT-4: llvm.call @obelisk_rt_v1_container_export_bitstream
// FEATURE: obelisk.feature.container_bitstream
// BYTECODE: intrinsic {{.*}}id=0x00010464 inputs=8 outputs=1 flags=0
// BYTECODE-COUNT-4: site {{.*}}id=0x00010464 inputs=
