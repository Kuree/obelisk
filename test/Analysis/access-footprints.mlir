// RUN: obelisk-opt %s --test-obelisk-access-footprints -o /dev/null 2> %t.threaded
// RUN: obelisk-opt %s --test-obelisk-access-footprints --mlir-disable-threading -o /dev/null 2> %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: FileCheck %s < %t.threaded

// Exact may footprints retain selector correlations. Unused X/Z elements must
// not enter a promotion certificate; unsupported or widened facts stay whole.
!row = !simulation.unpacked_array<0 : 2 x !simulation.logic<3>>
!matrix = !simulation.unpacked_array<0 : 2 x !row>
!record = !simulation.unpacked_struct<[#simulation.field<name = "lo", type = !simulation.logic<3>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "hi", type = !simulation.logic<5>, ordinal = 1, packedOffset = 0>]>
!records = !simulation.unpacked_array<0 : 2 x !record>
module {
  simulation.design @footprints {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !matrix design
    simulation.storage.decl 1 in 0 : !row design
    simulation.code_unit.decl 1 in 0 function hierarchy "skip_diagonal"
    simulation.func @skip_diagonal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!matrix>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = simulation.logic.constant 3 : i32, 0 : i32 : !simulation.logic<32>
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %il = simulation.logic.from_bits %i : i32 -> !simulation.logic<32>
      %ic = simulation.logic.compare slt %il, %limit : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
      %it = simulation.logic.is_true %ic : !simulation.logic<1>
      cf.cond_br %it, ^inner(%zero : i32), ^done
    ^inner(%j: i32):
      %jl = simulation.logic.from_bits %j : i32 -> !simulation.logic<32>
      %jc = simulation.logic.compare slt %jl, %limit : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
      %jt = simulation.logic.is_true %jc : !simulation.logic<1>
      cf.cond_br %jt, ^select, ^outer_next
    ^select:
      %row = simulation.ref.array_element %root[%i] : (!simulation.ref<!matrix>, i32) -> !simulation.ref<!row>
      %element = simulation.ref.array_element %row[%j] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %different = arith.cmpi ne, %i, %j : i32
      cf.cond_br %different, ^access, ^inner_next
    ^access:
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.ref.store %value to %element {test.footprint = "write"} : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      cf.br ^inner_next
    ^inner_next:
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^outer_next:
      %next_i = arith.addi %i, %one : i32
      cf.br ^outer(%next_i : i32)
    ^done:
      simulation.return
    }
// CHECK: skip_diagonal: read [3,12) [15,24)
// CHECK: skip_diagonal: write [3,12) [15,24)
    simulation.code_unit.decl 2 in 0 function hierarchy "include_diagonal"
    simulation.func @include_diagonal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!matrix>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = simulation.logic.constant 3 : i32, 0 : i32 : !simulation.logic<32>
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %il = simulation.logic.from_bits %i : i32 -> !simulation.logic<32>
      %ic = simulation.logic.compare slt %il, %limit : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
      %it = simulation.logic.is_true %ic : !simulation.logic<1>
      cf.cond_br %it, ^inner(%zero : i32), ^done
    ^inner(%j: i32):
      %jl = simulation.logic.from_bits %j : i32 -> !simulation.logic<32>
      %jc = simulation.logic.compare slt %jl, %limit : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
      %jt = simulation.logic.is_true %jc : !simulation.logic<1>
      cf.cond_br %jt, ^select, ^outer_next
    ^select:
      %row = simulation.ref.array_element %root[%i] : (!simulation.ref<!matrix>, i32) -> !simulation.ref<!row>
      %element = simulation.ref.array_element %row[%j] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      cf.br ^access
    ^access:
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.ref.store %value to %element {test.footprint = "write"} : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      cf.br ^inner_next
    ^inner_next:
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^outer_next:
      %next_i = arith.addi %i, %one : i32
      cf.br ^outer(%next_i : i32)
    ^done:
      simulation.return
    }
// CHECK: include_diagonal: read [0,27)
// CHECK: include_diagonal: write [0,27)
    simulation.code_unit.decl 3 in 0 function hierarchy "correlated_phi"
    simulation.func @correlated_phi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      %two = arith.constant 2 : i32
      cf.cond_br %condition, ^join(%zero, %two : i32, i32), ^join(%two, %zero : i32, i32)
    ^join(%a: i32, %b: i32):
      %same = arith.cmpi eq, %a, %b : i32
      cf.cond_br %same, ^impossible, ^access
    ^impossible:
      %one = arith.constant 1 : i32
      cf.br ^load(%one : i32)
    ^access:
      cf.br ^load(%a : i32)
    ^load(%index: i32):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: correlated_phi: read [0,3) [6,9)
    simulation.code_unit.decl 4 in 0 function hierarchy "unknown_index"
    simulation.func @unknown_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: unknown_index: read conservative
    simulation.code_unit.decl 5 in 0 function hierarchy "unknown_logic_index"
    simulation.func @unknown_logic_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %index = simulation.logic.constant 0 : i32, 1 : i32 : !simulation.logic<32>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, !simulation.logic<32>) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: unknown_logic_index: read conservative
    simulation.code_unit.decl 6 in 0 function hierarchy "out_of_bounds_path"
    simulation.func @out_of_bounds_path(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      %bad = arith.constant 3 : i32
      cf.cond_br %condition, ^join(%zero : i32), ^join(%bad : i32)
    ^join(%index: i32):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: out_of_bounds_path: read conservative
    simulation.code_unit.decl 7 in 0 function hierarchy "same_root_phi"
    simulation.func @same_root_phi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      %two = arith.constant 2 : i32
      %a = simulation.ref.array_element %root[%zero] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %b = simulation.ref.array_element %root[%two] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      cf.cond_br %condition, ^join(%a : !simulation.ref<!simulation.logic<3>>), ^join(%b : !simulation.ref<!simulation.logic<3>>)
    ^join(%element: !simulation.ref<!simulation.logic<3>>):
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: same_root_phi: read [0,3) [6,9)
    simulation.code_unit.decl 8 in 0 function hierarchy "projected_capture"
    simulation.func @projected_capture(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}, %root: !simulation.ref<!row> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !matrix, simulation.descriptor_low = 9 : i64, simulation.descriptor_indices = array<i64: 1>, simulation.descriptor_aggregate_type = !row})
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %zero = arith.constant 0 : i32
      %two = arith.constant 2 : i32
      cf.cond_br %condition, ^join(%zero : i32), ^join(%two : i32)
    ^join(%index: i32):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: projected_capture: read [9,12) [15,18)
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<-3 : -1 x !simulation.logic<3>> design
    simulation.code_unit.decl 9 in 0 function hierarchy "negative_unpacked"
    simulation.func @negative_unpacked(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %root = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<-3 : -1 x !simulation.logic<3>>>
      %a = arith.constant -3 : i65
      %b = arith.constant -1 : i65
      cf.cond_br %condition, ^join(%a : i65), ^join(%b : i65)
    ^join(%index: i65):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<-3 : -1 x !simulation.logic<3>>>, i65) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: negative_unpacked: read [0,3) [6,9)
    simulation.storage.decl 3 in 0 : !simulation.unpacked_array<7 : 5 x !simulation.logic<3>> design
    simulation.code_unit.decl 10 in 0 function hierarchy "descending_unpacked"
    simulation.func @descending_unpacked(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %root = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.unpacked_array<7 : 5 x !simulation.logic<3>>>
      %a = arith.constant 7 : i65
      %b = arith.constant 5 : i65
      cf.cond_br %condition, ^join(%a : i65), ^join(%b : i65)
    ^join(%index: i65):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<7 : 5 x !simulation.logic<3>>>, i65) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: descending_unpacked: read [0,3) [6,9)
    simulation.storage.decl 4 in 0 : !simulation.packed_array<7 : 5 x !simulation.logic<3>> design
    simulation.code_unit.decl 11 in 0 function hierarchy "descending_packed"
    simulation.func @descending_packed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %root = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.packed_array<7 : 5 x !simulation.logic<3>>>
      %a = arith.constant 7 : i65
      %b = arith.constant 6 : i65
      cf.cond_br %condition, ^join(%a : i65), ^join(%b : i65)
    ^join(%index: i65):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.packed_array<7 : 5 x !simulation.logic<3>>>, i65) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: descending_packed: read [3,9)
    simulation.storage.decl 5 in 0 : !simulation.packed_array<-3 : -1 x !simulation.logic<3>> design
    simulation.code_unit.decl 12 in 0 function hierarchy "ascending_packed"
    simulation.func @ascending_packed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %root = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.packed_array<-3 : -1 x !simulation.logic<3>>>
      %a = arith.constant -3 : i65
      %b = arith.constant -2 : i65
      cf.cond_br %condition, ^join(%a : i65), ^join(%b : i65)
    ^join(%index: i65):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.packed_array<-3 : -1 x !simulation.logic<3>>>, i65) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: ascending_packed: read [3,9)
    simulation.storage.decl 6 in 0 : !simulation.logic<27> design
    simulation.code_unit.decl 13 in 0 function hierarchy "dynamic_part_select"
    simulation.func @dynamic_part_select(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %root = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<27>>
      %a = arith.constant 1 : i32
      %b = arith.constant 20 : i32
      cf.cond_br %condition, ^join(%a : i32), ^join(%b : i32)
    ^join(%index: i32):
      %element = simulation.ref.dyn_extract %root from %index : (!simulation.ref<!simulation.logic<27>>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: dynamic_part_select: read [1,4) [20,23)
    simulation.code_unit.decl 14 in 0 function hierarchy "widening"
    simulation.func @widening(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 300 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%iteration: i32):
      %more = arith.cmpi slt, %iteration, %limit : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      %index = arith.andi %iteration, %one : i32
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %next = arith.addi %iteration, %one : i32
      cf.br ^loop(%next : i32)
    ^done:
      simulation.return
    }
// CHECK: widening: read conservative
    simulation.code_unit.decl 15 in 0 function hierarchy "unknown_join"
    simulation.func @unknown_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}, %unknown: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      cf.cond_br %condition, ^join(%zero : i32), ^join(%unknown : i32)
    ^join(%index: i32):
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: unknown_join: read conservative
    simulation.storage.decl 7 in 0 : !row design
    simulation.code_unit.decl 16 in 0 function hierarchy "different_root_phi"
    simulation.func @different_root_phi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %other = simulation.context.storage %ctx[7] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      %two = arith.constant 2 : i32
      %a = simulation.ref.array_element %root[%zero] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %b = simulation.ref.array_element %other[%two] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      cf.cond_br %condition, ^join(%a : !simulation.ref<!simulation.logic<3>>), ^join(%b : !simulation.ref<!simulation.logic<3>>)
    ^join(%element: !simulation.ref<!simulation.logic<3>>):
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: different_root_phi: read conservative
    simulation.code_unit.decl 17 in 0 function hierarchy "parallel_phi_swap"
    simulation.func @parallel_phi_swap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 17 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      cf.br ^loop(%zero, %two, %zero : i32, i32, i32)
    ^loop(%a: i32, %b: i32, %count: i32):
      %more = arith.cmpi slt, %count, %two : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      %element = simulation.ref.array_element %root[%a] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %next = arith.addi %count, %one : i32
      cf.br ^loop(%b, %a, %next : i32, i32, i32)
    ^done:
      simulation.return
    }
// CHECK: parallel_phi_swap: read [0,3) [6,9)
    simulation.storage.decl 8 in 0 : !simulation.unpacked_array<0 : 3 x !simulation.logic<3>> design
    simulation.code_unit.decl 18 in 0 function hierarchy "wrapping_index"
    simulation.func @wrapping_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 18 : i64} {
      %root = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<3>>>
      %zero = arith.constant 0 : i2
      %one = arith.constant 1 : i2
      cf.br ^loop(%zero : i2)
    ^loop(%index: i2):
      %not_one = arith.cmpi ne, %index, %one : i2
      cf.cond_br %not_one, ^access, ^next
    ^access:
      %wide = arith.extui %index : i2 to i65
      %element = simulation.ref.array_element %root[%wide] : (!simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<3>>>, i65) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      cf.br ^next
    ^next:
      %increment = arith.addi %index, %one : i2
      %again = arith.cmpi ne, %increment, %zero : i2
      cf.cond_br %again, ^loop(%increment : i2), ^done
    ^done:
      simulation.return
    }
// CHECK: wrapping_index: read [0,3) [6,12)
    simulation.storage.decl 9 in 0 : !records design
    simulation.code_unit.decl 19 in 0 function hierarchy "field_of_selected_element"
    simulation.func @field_of_selected_element(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 19 : i64} {
      %root = simulation.context.storage %ctx[9] : !simulation.ref<!records>
      %zero = arith.constant 0 : i32
      %two = arith.constant 2 : i32
      cf.cond_br %condition, ^join(%zero : i32), ^join(%two : i32)
    ^join(%index: i32):
      %record = simulation.ref.array_element %root[%index] : (!simulation.ref<!records>, i32) -> !simulation.ref<!record>
      %field = simulation.ref.subelement %record[[1]] : !simulation.ref<!record> -> !simulation.ref<!simulation.logic<5>>
      %element = simulation.ref.extract %field from 1 : !simulation.ref<!simulation.logic<5>> -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: field_of_selected_element: read [4,7) [20,23)
    simulation.code_unit.decl 20 in 0 function hierarchy "helper"
    simulation.func @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 20 : i64} {
      %two = arith.constant 2 : i32
      simulation.return %two : i32
    }
    simulation.code_unit.decl 21 in 0 function hierarchy "call_result"
    simulation.func @call_result(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %index = simulation.call @helper(%ctx) : (!simulation.context) -> i32
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: call_result: read conservative
    simulation.code_unit.decl 22 in 0 function hierarchy "index_cast"
    simulation.func @index_cast(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %unknown: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 22 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!row>
      %host_index = arith.index_cast %unknown : i64 to index
      %index = arith.index_cast %host_index : index to i32
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element {test.footprint = "read"} : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.return
    }
// CHECK: index_cast: read conservative
  }
}
