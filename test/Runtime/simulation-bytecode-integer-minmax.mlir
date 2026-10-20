// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Integer min/max used by capture-domain intersections selects the proper
// signed or unsigned operand, including one-bit, equal, and multiword values.
// CHECK: integer minmax valid 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @integer_minmax {
    simulation.scope.decl 0 hierarchy "integer_minmax"
    simulation.code_unit.decl 9982100 in 0 root_initializer hierarchy "integer_minmax.root"
    simulation.code_unit.decl 9982101 in 0 initial hierarchy "integer_minmax.initial"
    simulation.code_unit.decl 9982102 in 0 function hierarchy "integer_minmax.check1"
    simulation.code_unit.decl 9982103 in 0 function hierarchy "integer_minmax.check5"
    simulation.code_unit.decl 9982104 in 0 function hierarchy "integer_minmax.check64"
    simulation.code_unit.decl 9982105 in 0 function hierarchy "integer_minmax.check130"
    simulation.func @__obelisk_root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 9982100 : i64} {
      %process = simulation.spawn @initial(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @check1(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %lhs: i1 {simulation.capture_kind = 1 : i32},
      %rhs: i1 {simulation.capture_kind = 1 : i32},
      %umin: i1 {simulation.capture_kind = 1 : i32},
      %umax: i1 {simulation.capture_kind = 1 : i32},
      %smin: i1 {simulation.capture_kind = 1 : i32},
      %smax: i1 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 9982102 : i64} {
      %rumin = arith.minui %lhs, %rhs : i1
      %okumin = arith.cmpi eq, %rumin, %umin : i1
      %rumax = arith.maxui %lhs, %rhs : i1
      %okumax = arith.cmpi eq, %rumax, %umax : i1
      %rsmin = arith.minsi %lhs, %rhs : i1
      %oksmin = arith.cmpi eq, %rsmin, %smin : i1
      %rsmax = arith.maxsi %lhs, %rhs : i1
      %oksmax = arith.cmpi eq, %rsmax, %smax : i1
      %ok0 = arith.andi %okumin, %okumax : i1
      %ok1 = arith.andi %oksmin, %oksmax : i1
      %ok = arith.andi %ok0, %ok1 : i1
      simulation.return %ok : i1
    }
    simulation.func private @check5(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %lhs: i5 {simulation.capture_kind = 1 : i32},
      %rhs: i5 {simulation.capture_kind = 1 : i32},
      %umin: i5 {simulation.capture_kind = 1 : i32},
      %umax: i5 {simulation.capture_kind = 1 : i32},
      %smin: i5 {simulation.capture_kind = 1 : i32},
      %smax: i5 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 9982103 : i64} {
      %rumin = arith.minui %lhs, %rhs : i5
      %okumin = arith.cmpi eq, %rumin, %umin : i5
      %rumax = arith.maxui %lhs, %rhs : i5
      %okumax = arith.cmpi eq, %rumax, %umax : i5
      %rsmin = arith.minsi %lhs, %rhs : i5
      %oksmin = arith.cmpi eq, %rsmin, %smin : i5
      %rsmax = arith.maxsi %lhs, %rhs : i5
      %oksmax = arith.cmpi eq, %rsmax, %smax : i5
      %ok0 = arith.andi %okumin, %okumax : i1
      %ok1 = arith.andi %oksmin, %oksmax : i1
      %ok = arith.andi %ok0, %ok1 : i1
      simulation.return %ok : i1
    }
    simulation.func private @check64(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %lhs: i64 {simulation.capture_kind = 1 : i32},
      %rhs: i64 {simulation.capture_kind = 1 : i32},
      %umin: i64 {simulation.capture_kind = 1 : i32},
      %umax: i64 {simulation.capture_kind = 1 : i32},
      %smin: i64 {simulation.capture_kind = 1 : i32},
      %smax: i64 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 9982104 : i64} {
      %rumin = arith.minui %lhs, %rhs : i64
      %okumin = arith.cmpi eq, %rumin, %umin : i64
      %rumax = arith.maxui %lhs, %rhs : i64
      %okumax = arith.cmpi eq, %rumax, %umax : i64
      %rsmin = arith.minsi %lhs, %rhs : i64
      %oksmin = arith.cmpi eq, %rsmin, %smin : i64
      %rsmax = arith.maxsi %lhs, %rhs : i64
      %oksmax = arith.cmpi eq, %rsmax, %smax : i64
      %ok0 = arith.andi %okumin, %okumax : i1
      %ok1 = arith.andi %oksmin, %oksmax : i1
      %ok = arith.andi %ok0, %ok1 : i1
      simulation.return %ok : i1
    }
    simulation.func private @check130(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %lhs: i130 {simulation.capture_kind = 1 : i32},
      %rhs: i130 {simulation.capture_kind = 1 : i32},
      %umin: i130 {simulation.capture_kind = 1 : i32},
      %umax: i130 {simulation.capture_kind = 1 : i32},
      %smin: i130 {simulation.capture_kind = 1 : i32},
      %smax: i130 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 9982105 : i64} {
      %rumin = arith.minui %lhs, %rhs : i130
      %okumin = arith.cmpi eq, %rumin, %umin : i130
      %rumax = arith.maxui %lhs, %rhs : i130
      %okumax = arith.cmpi eq, %rumax, %umax : i130
      %rsmin = arith.minsi %lhs, %rhs : i130
      %oksmin = arith.cmpi eq, %rsmin, %smin : i130
      %rsmax = arith.maxsi %lhs, %rhs : i130
      %oksmax = arith.cmpi eq, %rsmax, %smax : i130
      %ok0 = arith.andi %okumin, %okumax : i1
      %ok1 = arith.andi %oksmin, %oksmax : i1
      %ok = arith.andi %ok0, %ok1 : i1
      simulation.return %ok : i1
    }
    simulation.func private @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9982101 : i64} {
      %v0_1 = arith.constant 0 : i1
      %v0_2 = arith.constant 1 : i1
      %v0_3 = arith.constant 0 : i1
      %v0_4 = arith.constant 1 : i1
      %v0_5 = arith.constant 1 : i1
      %v0_6 = arith.constant 0 : i1
      %ok0 = simulation.call @check1(%ctx, %v0_1, %v0_2, %v0_3, %v0_4, %v0_5, %v0_6) : (!simulation.context, i1, i1, i1, i1, i1, i1) -> i1
      %v1_1 = arith.constant 1 : i1
      %v1_2 = arith.constant 1 : i1
      %v1_3 = arith.constant 1 : i1
      %v1_4 = arith.constant 1 : i1
      %v1_5 = arith.constant 1 : i1
      %v1_6 = arith.constant 1 : i1
      %ok1 = simulation.call @check1(%ctx, %v1_1, %v1_2, %v1_3, %v1_4, %v1_5, %v1_6) : (!simulation.context, i1, i1, i1, i1, i1, i1) -> i1
      %all1 = arith.andi %ok0, %ok1 : i1
      %v2_1 = arith.constant 16 : i5
      %v2_2 = arith.constant 15 : i5
      %v2_3 = arith.constant 15 : i5
      %v2_4 = arith.constant 16 : i5
      %v2_5 = arith.constant 16 : i5
      %v2_6 = arith.constant 15 : i5
      %ok2 = simulation.call @check5(%ctx, %v2_1, %v2_2, %v2_3, %v2_4, %v2_5, %v2_6) : (!simulation.context, i5, i5, i5, i5, i5, i5) -> i1
      %all2 = arith.andi %all1, %ok2 : i1
      %v3_1 = arith.constant 31 : i5
      %v3_2 = arith.constant 0 : i5
      %v3_3 = arith.constant 0 : i5
      %v3_4 = arith.constant 31 : i5
      %v3_5 = arith.constant 31 : i5
      %v3_6 = arith.constant 0 : i5
      %ok3 = simulation.call @check5(%ctx, %v3_1, %v3_2, %v3_3, %v3_4, %v3_5, %v3_6) : (!simulation.context, i5, i5, i5, i5, i5, i5) -> i1
      %all3 = arith.andi %all2, %ok3 : i1
      %v4_1 = arith.constant 9223372036854775808 : i64
      %v4_2 = arith.constant 9223372036854775807 : i64
      %v4_3 = arith.constant 9223372036854775807 : i64
      %v4_4 = arith.constant 9223372036854775808 : i64
      %v4_5 = arith.constant 9223372036854775808 : i64
      %v4_6 = arith.constant 9223372036854775807 : i64
      %ok4 = simulation.call @check64(%ctx, %v4_1, %v4_2, %v4_3, %v4_4, %v4_5, %v4_6) : (!simulation.context, i64, i64, i64, i64, i64, i64) -> i1
      %all4 = arith.andi %all3, %ok4 : i1
      %v5_1 = arith.constant 18446744073709551615 : i64
      %v5_2 = arith.constant 18446744073709551615 : i64
      %v5_3 = arith.constant 18446744073709551615 : i64
      %v5_4 = arith.constant 18446744073709551615 : i64
      %v5_5 = arith.constant 18446744073709551615 : i64
      %v5_6 = arith.constant 18446744073709551615 : i64
      %ok5 = simulation.call @check64(%ctx, %v5_1, %v5_2, %v5_3, %v5_4, %v5_5, %v5_6) : (!simulation.context, i64, i64, i64, i64, i64, i64) -> i1
      %all5 = arith.andi %all4, %ok5 : i1
      %v6_1 = arith.constant 680564733841876926926749214863536422912 : i130
      %v6_2 = arith.constant 680564733841876926926749214863536422911 : i130
      %v6_3 = arith.constant 680564733841876926926749214863536422911 : i130
      %v6_4 = arith.constant 680564733841876926926749214863536422912 : i130
      %v6_5 = arith.constant 680564733841876926926749214863536422912 : i130
      %v6_6 = arith.constant 680564733841876926926749214863536422911 : i130
      %ok6 = simulation.call @check130(%ctx, %v6_1, %v6_2, %v6_3, %v6_4, %v6_5, %v6_6) : (!simulation.context, i130, i130, i130, i130, i130, i130) -> i1
      %all6 = arith.andi %all5, %ok6 : i1
      %v7_1 = arith.constant 18446744073709551616 : i130
      %v7_2 = arith.constant 18446744073709551617 : i130
      %v7_3 = arith.constant 18446744073709551616 : i130
      %v7_4 = arith.constant 18446744073709551617 : i130
      %v7_5 = arith.constant 18446744073709551616 : i130
      %v7_6 = arith.constant 18446744073709551617 : i130
      %ok7 = simulation.call @check130(%ctx, %v7_1, %v7_2, %v7_3, %v7_4, %v7_5, %v7_6) : (!simulation.context, i130, i130, i130, i130, i130, i130) -> i1
      %all7 = arith.andi %all6, %ok7 : i1
      %format = simulation.bytes.constant "integer minmax valid %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %all7) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i1
      simulation.return
    }
  }
}
