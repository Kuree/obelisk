// RUN: talos-opt %s --convert-talos-sim-values-to-standard | FileCheck %s
// Lane conversion uses ordinary LLVM vector integer conversions. Both logic
// planes preserve their independent bits, including Z, as in LRM 11.4.8.

// CHECK-LABEL: func.func @expand
// CHECK: llvm.bitcast {{.*}} : i8 to vector<8xi1>
// CHECK: llvm.zext {{.*}} : vector<8xi1> to vector<8xi8>
// CHECK: llvm.bitcast {{.*}} : vector<8xi8> to i64
func.func @expand(%input: i8) -> i64 {
  %r = schedule.word.resize_lanes %input {input_lane_width = 1 : i32, result_lane_width = 8 : i32} : (i8) -> i64
  return %r : i64
}
// CHECK-LABEL: func.func @compact
// CHECK: llvm.bitcast {{.*}} : i64 to vector<8xi8>
// CHECK: llvm.trunc {{.*}} : vector<8xi8> to vector<8xi3>
// CHECK: llvm.bitcast {{.*}} : vector<8xi3> to i24
// CHECK: llvm.bitcast {{.*}} : i64 to vector<8xi8>
// CHECK: llvm.trunc {{.*}} : vector<8xi8> to vector<8xi3>
// CHECK: llvm.bitcast {{.*}} : vector<8xi3> to i24
func.func @compact(%input: !simulation.logic<64>) -> !simulation.logic<24> {
  %r = schedule.word.resize_lanes %input {input_lane_width = 8 : i32, result_lane_width = 3 : i32} : (!simulation.logic<64>) -> !simulation.logic<24>
  return %r : !simulation.logic<24>
}
