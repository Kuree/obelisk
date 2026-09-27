// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @recursive_bitstream {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.cast"
    simulation.func @cast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.dynamic_array<!simulation.dynamic_array<i8>>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %result0, %matched0, %watch0 =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<i8>>) ->
              (i32, i1, !simulation.managed_watch)
      %result1, %matched1, %watch1 =
          simulation.recursive.export_bitstream %source {
            observe,
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<i8>>) ->
              (i32, i1, !simulation.managed_watch)
      simulation.return
    }

    simulation.code_unit.decl 2 in 0 initial hierarchy "top.wide"
    simulation.func @wide(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.unpacked_array<0 : 8191 x
            !simulation.dynamic_array<i8>>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %result, %matched, %watch =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 3, 524288, 0,
                8589934594, 0, 8192, 64, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.unpacked_array<0 : 8191 x
              !simulation.dynamic_array<i8>>) ->
              (i8, i1, !simulation.managed_watch)
      simulation.return
    }

    simulation.code_unit.decl 3 in 0 initial hierarchy "top.string"
    simulation.func @string(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.string
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %result, %matched, %watch =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 1, 64, 0,
                4, 0, 0, 0, 64, 0>
          } : (!simulation.string) ->
              (i24, i1, !simulation.managed_watch)
      simulation.return
    }
  }
}

// NATIVE-COUNT-4: llvm.call @obelisk_rt_v1_recursive_export_bitstream
// NATIVE-NOT: aggregate.extract
// NATIVE-NOT: dyn_insert
// BYTECODE: intrinsic {{.*}}id=0x00010464 inputs=2 outputs=3 flags=1
// BYTECODE: intrinsic {{.*}}id=0x00010464 inputs=2 outputs=3 flags=2
