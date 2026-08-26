// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --implicit-check-not=i1048576 \
// RUN:       --implicit-check-not='llvm.func @wide_export('

// A descriptor-based native export must not make LLVM legalize million-bit
// call arguments or plane loads/stores. The two four-state planes are copied
// independently, and the now-unreferenced typed export body is removed.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk_sim.has_dpi_exports
} {
  obelisk_sim.design @wide {
    obelisk_sim.scope.decl 0 hierarchy "wide"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "wide.root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "wide.export"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @wide_export(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<1048576>
            {obelisk_sim.capture_kind = 2 : i32})
        -> !obelisk_sim.logic<1048576>
        attributes {
          code_unit_id = 2 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "wide_export_c",
          obelisk_sim.dpi_export_id = 900 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = logic_vector, direction = input,
                                  width = 1048576, fourState = true,
                                  isSigned = false>,
            #obelisk_sim.dpi_abi<kind = logic_vector, direction = result,
                                  width = 1048576, fourState = true,
                                  isSigned = false>
          ]
        } {
      obelisk_sim.return %value : !obelisk_sim.logic<1048576>
    }
  }
}

// CHECK-LABEL: llvm.func @wide_export_c
// CHECK: llvm.intr.memset
// CHECK: llvm.intr.memset
// CHECK-LABEL: llvm.func @wide_export.__obelisk_dpi_export_bridge.__obelisk_dpi_export
// CHECK-COUNT-2: llvm.intr.memcpy
