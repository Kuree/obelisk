// RUN: talos-opt %s | talos-opt | FileCheck %s

// CHECK-LABEL: func.func private @import
// CHECK-SAME: dpi.abi_signature = [#dpi.abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #dpi.abi<kind = unpacked_aggregate, direction = output, width = 64, fourState = false, isSigned = false>]
// CHECK-SAME: dpi.aggregate_layouts = [unit, #dpi.aggregate_abi<cSize = 8, cAlignment = 4, stringCount = 0, leaves = [0, 0, 0, 4, 32, 1, 0, 0]>]
// CHECK-SAME: dpi.c_identifier = "c_import"
// CHECK-SAME: dpi.import
// CHECK-SAME: dpi.import_id = 7 : i32
// CHECK-SAME: dpi.logical_inputs = 1 : i32
// CHECK-SAME: dpi.pure
func.func private @import() attributes {
  dpi.import, dpi.pure, dpi.import_id = 7 : i32,
  dpi.c_identifier = "c_import", dpi.logical_inputs = 1 : i32,
  dpi.abi_signature = [
    #dpi.abi<kind = int, direction = input, width = 32, fourState = false,
             isSigned = true>,
    #dpi.abi<kind = unpacked_aggregate, direction = output, width = 64,
             fourState = false, isSigned = false>],
  dpi.aggregate_layouts = [
    unit,
    #dpi.aggregate_abi<cSize = 8, cAlignment = 4, stringCount = 0,
                       leaves = [0, 0, 0, 4, 32, 1, 0, 0]>]}

// CHECK-LABEL: func.func private @export_bridge
// CHECK-SAME: dpi.elided_inputs = array<i64: 0, 2>
// CHECK-SAME: dpi.export_body_symbol = "body"
// CHECK-SAME: dpi.export_bridge
func.func private @export_bridge() attributes {
  dpi.export_bridge, dpi.export_body_symbol = "body",
  dpi.elided_inputs = array<i64: 0, 2>}

// CHECK: #dpi.open_array_abi<storage = 0
func.func private @open_array() attributes {
  layout = #dpi.open_array_abi<storage = 0, elementKind = bit,
      elementWidth = 1, transportWidth = 8, transportFourState = false,
      fourState = false, elementCSize = 1, elementCAlignment = 1,
      elementStringCount = 0, elementLeaves = [0, 0, 0, 0, 1, 0, 0, 0],
      packedLeft = 0, packedRight = 0, ranges = [7, 0],
      sourceRanges = [7, 0], shapePlan = []>}
