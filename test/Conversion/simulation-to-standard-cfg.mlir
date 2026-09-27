// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | FileCheck %s --implicit-check-not=!simulation.logic \
// RUN:       --implicit-check-not=unrealized_conversion_cast \
// RUN:       --implicit-check-not=test.value

// CHECK-LABEL: func.func @unreachable()
// CHECK: return
// CHECK: ^bb1(%{{.*}}: i5, %{{.*}}: i5):
// CHECK: return
func.func @unreachable() {
  return
^bb1(%unused: !simulation.logic<5>):
  return
}

// CHECK-LABEL: func.func @switch_cfg(
// CHECK: cf.switch %{{.*}} : i32, [
// CHECK: default: ^bb1(%{{.*}}, %{{.*}} : i5, i5),
// CHECK: 0: ^bb2(%{{.*}}, %{{.*}} : i5, i5)
// CHECK: ] {test.switch = "keep"}
// CHECK: cf.br ^bb3({{.*}} : i5, i5) {test.branch = "keep"}
func.func @switch_cfg(%flag: i32, %input: !simulation.logic<5>)
    -> !simulation.logic<5> {
  cf.switch %flag : i32, [
    default: ^bb1(%input : !simulation.logic<5>),
    0: ^bb2(%input : !simulation.logic<5>)
  ] {test.switch = "keep"}
^bb1(%default_arg: !simulation.logic<5>):
  cf.br ^bb3(%default_arg : !simulation.logic<5>) {test.branch = "keep"}
^bb2(%case_arg: !simulation.logic<5>):
  cf.br ^bb3(%case_arg : !simulation.logic<5>)
^bb3(%result: !simulation.logic<5>):
  return %result : !simulation.logic<5>
}

// CHECK-LABEL: func.func @metadata_identity(
// CHECK-SAME: i1 {test.formal = "keep"}
// CHECK-SAME: i1 {test.formal = "keep"}
// CHECK-SAME: -> (i1 {test.result = "keep"}, i1 {test.result = "keep"})
// CHECK-SAME: attributes {no_inline}
// CHECK-LABEL: func.func @metadata()
// CHECK: %{{.*}}:2 = call @metadata_identity({{.*}}) {arg_attrs = [{test.actual = "keep"}, {test.actual = "keep"}], no_inline, res_attrs = [{test.call_result = "keep"}, {test.call_result = "keep"}], test.call = "keep"} : (i1, i1) -> (i1, i1)
// CHECK: return {test.return = "keep"} {{.*}} : i1, i1
func.func @metadata_identity(
    %input: !simulation.logic<1> {test.formal = "keep"})
    -> !simulation.logic<1>
    attributes {no_inline, res_attrs = [{test.result = "keep"}]} {
  return %input : !simulation.logic<1>
}

func.func @metadata() -> !simulation.logic<1> {
  %value = simulation.logic.constant 1 : i1, 0 : i1
      {test.value = "keep"} : !simulation.logic<1>
  %called = func.call @metadata_identity(%value) {
      arg_attrs = [{test.actual = "keep"}], no_inline,
      res_attrs = [{test.call_result = "keep"}], test.call = "keep"
    } : (!simulation.logic<1>) -> !simulation.logic<1>
  return {test.return = "keep"} %called : !simulation.logic<1>
}
