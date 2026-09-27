// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --implicit-check-not=i1048576 \
// RUN:       --implicit-check-not=i65536 \
// RUN:       --implicit-check-not='llvm.func @wide_export('

// A descriptor-based native export must not make LLVM legalize million-bit
// call arguments or plane loads/stores. The two four-state planes are copied
// independently, and the now-unreferenced typed export body is removed.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  simulation.has_dpi_exports
} {
  simulation.design @wide {
    simulation.scope.decl 0 hierarchy "wide"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "wide.root"
    simulation.code_unit.decl 2 in 0 function hierarchy "wide.export"
    simulation.code_unit.decl 3 in 0 task hierarchy "wide.aggregate_export"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      simulation.return
    }

    simulation.func @wide_export(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<1048576>
            {simulation.capture_kind = 2 : i32})
        -> !simulation.logic<1048576>
        attributes {
          code_unit_id = 2 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "wide_export_c",
          simulation.dpi_export_id = 900 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = logic_vector, direction = input,
                                  width = 1048576, fourState = true,
                                  isSigned = false>,
            #simulation.dpi_abi<kind = logic_vector, direction = result,
                                  width = 1048576, fourState = true,
                                  isSigned = false>
          ]
        } {
      simulation.return %value : !simulation.logic<1048576>
    }

    simulation.func @wide_aggregate_export(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.unpacked_array<0 : 8191 x !simulation.logic<8>>>
            {simulation.capture_kind = 1 : i32})
        attributes {
          code_unit_id = 3 : i64, entry_kind = 12 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "wide_aggregate_export_c",
          simulation.dpi_export_id = 901 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_elided_inputs = array<i64: 0>,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = unpacked_aggregate,
                                  direction = output, width = 65536,
                                  fourState = true, isSigned = false>,
            #simulation.dpi_abi<kind = unpacked_aggregate,
                                  direction = output, width = 65536,
                                  fourState = true, isSigned = false>
          ],
          simulation.dpi_aggregate_layouts = [
            #simulation.dpi_aggregate_abi<cSize = 65536, cAlignment = 4,
                stringCount = 0,
                leaves = [1, 0, 0, 8192, 8, 8, 0, 1, 0, 0, 0, 7, 8, 1, 0, 0]>,
            #simulation.dpi_aggregate_abi<cSize = 65536, cAlignment = 4,
                stringCount = 0,
                leaves = [1, 0, 0, 8192, 8, 8, 0, 1, 0, 0, 0, 7, 8, 1, 0, 0]>
          ]
        } {
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @wide_export_c
// CHECK: llvm.intr.memset
// CHECK: llvm.intr.memset
// CHECK-LABEL: llvm.func @wide_aggregate_export_c
// CHECK-COUNT-4: llvm.intr.memset
// CHECK-LABEL: llvm.func @wide_export.__obelisk_dpi_export_bridge.__obelisk_dpi_export
// CHECK-COUNT-2: llvm.intr.memcpy
