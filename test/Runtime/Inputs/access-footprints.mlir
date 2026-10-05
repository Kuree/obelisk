!row = !simulation.unpacked_array<0 : 2 x !simulation.logic<3>>
!matrix = !simulation.unpacked_array<0 : 2 x !row>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32,
  obelisk.native.closed_executable
} {
  simulation.design @access_promotion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "reset.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "reset.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "reset.lane"
    simulation.code_unit.decl 4 in 0 initial hierarchy "stimulus"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !matrix design
    simulation.storage.decl 2 in 0 : !simulation.logic<3> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %zero_clock = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero_clock to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %stimulus = simulation.spawn @stimulus(%ctx) : !simulation.context -> !simulation.process
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %lane = simulation.spawn @lane(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @lane(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!matrix>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<3>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %clear = simulation.logic.constant 0 : i3, 0 : i3 : !simulation.logic<3>
      simulation.ref.store %clear to %output : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more_i = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more_i, ^inner(%zero : i32), ^done
    ^inner(%j: i32):
      %more_j = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %more_j, ^select, ^outer_next
    ^select:
      %different = arith.cmpi ne, %i, %j : i32
      cf.cond_br %different, ^access, ^inner_next
    ^access:
      %row = simulation.ref.array_element %root[%i] : (!simulation.ref<!matrix>, i32) -> !simulation.ref<!row>
      %element = simulation.ref.array_element %row[%j] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %old = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %next = simulation.logic.binary xor %old, %value : !simulation.logic<3>
      simulation.ref.store %next to %output : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      cf.br ^inner_next
    ^inner_next:
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^outer_next:
      %next_i = arith.addi %i, %one : i32
      cf.br ^outer(%next_i : i32)
    ^done:
      cf.br ^wait
    }
    simulation.func @stimulus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!matrix>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<3>>
      %delay = simulation.time.constant 4
      %channel = arith.constant 1 : i32
      %status = arith.constant 0 : i32
      %v0_0 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v0_1 = simulation.logic.constant 1 : i3, 0 : i3 : !simulation.logic<3>
      %v0_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v0_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v0_4 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v0_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v0_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v0_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v0_8 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %r0_0 = simulation.aggregate.construct %v0_0, %v0_1, %v0_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r0_1 = simulation.aggregate.construct %v0_3, %v0_4, %v0_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r0_2 = simulation.aggregate.construct %v0_6, %v0_7, %v0_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix0 = simulation.aggregate.construct %r0_0, %r0_1, %r0_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix0 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample0
    ^sample0:
      %sample0 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format0 = simulation.bytes.constant "footprint phase0=%b"
      simulation.display %ctx to %channel(%format0, %sample0) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      %v1_0 = simulation.logic.constant 7 : i3, 7 : i3 : !simulation.logic<3>
      %v1_1 = simulation.logic.constant 1 : i3, 0 : i3 : !simulation.logic<3>
      %v1_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v1_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v1_4 = simulation.logic.constant 7 : i3, 7 : i3 : !simulation.logic<3>
      %v1_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v1_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v1_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v1_8 = simulation.logic.constant 7 : i3, 7 : i3 : !simulation.logic<3>
      %r1_0 = simulation.aggregate.construct %v1_0, %v1_1, %v1_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r1_1 = simulation.aggregate.construct %v1_3, %v1_4, %v1_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r1_2 = simulation.aggregate.construct %v1_6, %v1_7, %v1_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix1 = simulation.aggregate.construct %r1_0, %r1_1, %r1_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix1 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample1
    ^sample1:
      %sample1 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format1 = simulation.bytes.constant "footprint phase1=%b"
      simulation.display %ctx to %channel(%format1, %sample1) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      %v2_0 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v2_1 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v2_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v2_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v2_4 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v2_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v2_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v2_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v2_8 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %r2_0 = simulation.aggregate.construct %v2_0, %v2_1, %v2_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r2_1 = simulation.aggregate.construct %v2_3, %v2_4, %v2_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r2_2 = simulation.aggregate.construct %v2_6, %v2_7, %v2_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix2 = simulation.aggregate.construct %r2_0, %r2_1, %r2_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix2 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample2
    ^sample2:
      %sample2 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format2 = simulation.bytes.constant "footprint phase2=%b"
      simulation.display %ctx to %channel(%format2, %sample2) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      %v3_0 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v3_1 = simulation.logic.constant 7 : i3, 7 : i3 : !simulation.logic<3>
      %v3_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v3_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v3_4 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v3_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v3_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v3_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v3_8 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %r3_0 = simulation.aggregate.construct %v3_0, %v3_1, %v3_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r3_1 = simulation.aggregate.construct %v3_3, %v3_4, %v3_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r3_2 = simulation.aggregate.construct %v3_6, %v3_7, %v3_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix3 = simulation.aggregate.construct %r3_0, %r3_1, %r3_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix3 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample3
    ^sample3:
      %sample3 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format3 = simulation.bytes.constant "footprint phase3=%b"
      simulation.display %ctx to %channel(%format3, %sample3) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      %v4_0 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v4_1 = simulation.logic.constant 1 : i3, 0 : i3 : !simulation.logic<3>
      %v4_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v4_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v4_4 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v4_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v4_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v4_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v4_8 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %r4_0 = simulation.aggregate.construct %v4_0, %v4_1, %v4_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r4_1 = simulation.aggregate.construct %v4_3, %v4_4, %v4_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r4_2 = simulation.aggregate.construct %v4_6, %v4_7, %v4_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix4 = simulation.aggregate.construct %r4_0, %r4_1, %r4_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix4 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample4
    ^sample4:
      %sample4 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format4 = simulation.bytes.constant "footprint phase4=%b"
      simulation.display %ctx to %channel(%format4, %sample4) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      %v5_0 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v5_1 = simulation.logic.constant 1 : i3, 0 : i3 : !simulation.logic<3>
      %v5_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v5_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v5_4 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v5_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v5_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v5_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v5_8 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %r5_0 = simulation.aggregate.construct %v5_0, %v5_1, %v5_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r5_1 = simulation.aggregate.construct %v5_3, %v5_4, %v5_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r5_2 = simulation.aggregate.construct %v5_6, %v5_7, %v5_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix5 = simulation.aggregate.construct %r5_0, %r5_1, %r5_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix5 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample5
    ^sample5:
      %sample5 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format5 = simulation.bytes.constant "footprint phase5=%b"
      simulation.display %ctx to %channel(%format5, %sample5) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      %v6_0 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v6_1 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v6_2 = simulation.logic.constant 2 : i3, 0 : i3 : !simulation.logic<3>
      %v6_3 = simulation.logic.constant 3 : i3, 0 : i3 : !simulation.logic<3>
      %v6_4 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %v6_5 = simulation.logic.constant 4 : i3, 0 : i3 : !simulation.logic<3>
      %v6_6 = simulation.logic.constant 5 : i3, 0 : i3 : !simulation.logic<3>
      %v6_7 = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %v6_8 = simulation.logic.constant 0 : i3, 7 : i3 : !simulation.logic<3>
      %r6_0 = simulation.aggregate.construct %v6_0, %v6_1, %v6_2 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r6_1 = simulation.aggregate.construct %v6_3, %v6_4, %v6_5 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %r6_2 = simulation.aggregate.construct %v6_6, %v6_7, %v6_8 : (!simulation.logic<3>, !simulation.logic<3>, !simulation.logic<3>) -> !row
      %matrix6 = simulation.aggregate.construct %r6_0, %r6_1, %r6_2 : (!row, !row, !row) -> !matrix
      simulation.ref.store %matrix6 to %root : !matrix, !simulation.ref<!matrix>
      simulation.suspend.delay %delay to ^sample6
    ^sample6:
      %sample6 = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %format6 = simulation.bytes.constant "footprint phase6=%b"
      simulation.display %ctx to %channel(%format6, %sample6) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !simulation.logic<3>
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
