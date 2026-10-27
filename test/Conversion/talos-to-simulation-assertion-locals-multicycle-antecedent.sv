// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null

// Deterministic multi-cycle antecedents carry initialized assertion locals
// through every live age. Ordered match assignments update that one attempt's
// values before an overlapped consequent or nonoverlapped handoff begins.
// A pending overlapped consequent is weak for assert-property and therefore
// dispatches its pass action once for the one live consequent age.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos.{{.*}}.weak(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK: simulation.ref.load
// CHECK-COUNT-1: simulation.spawn @unit_0.$concurrent_eos_report.{{.*}}.weak
// CHECK-NOT: simulation.spawn @unit_0.$concurrent_eos_report.{{.*}}.weak
// An antecedent that is still live at EOS has no match. Implication therefore
// completes vacuously true, independently of consequent strength.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos.{{.*}}.pass.antecedent_no_match(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-SAME: simulation.concurrent_eos_forced_completion
// CHECK-SAME: simulation.concurrent_eos_vacuous
// CHECK: simulation.ref.load
// CHECK-COUNT-1: simulation.spawn @unit_0.$concurrent_eos_report.{{.*}}.pass.antecedent_no_match
// CHECK-NOT: simulation.spawn @unit_0.$concurrent_eos_report.{{.*}}.pass.antecedent_no_match
// The terminal antecedent match call receives the value after its preceding
// blocking assignment and runs as a detached Reactive callback.
// CHECK: simulation.func private @[[MATCH_CALL:unit_0\.[^(]+]](
// CHECK-SAME: %arg1: !simulation.logic<1>
// CHECK-SAME: home_region = 10 : i32
// CHECK-SAME: simulation.concurrent_match_call
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK: %[[STATE:.*]] = simulation.ref.alloc
// CHECK: simulation.spawn @unit_0.$concurrent_eos.{{.*}}.weak({{.*}}, %[[STATE]])
// CHECK: simulation.spawn @unit_0.$concurrent_eos.{{.*}}.pass.antecedent_no_match({{.*}}, %[[STATE]])
// CHECK: simulation.suspend.edge posedge
// CHECK: arith.andi {{.*}}, %c8_i64{{.*}}
// CHECK: %[[ANTE_ACTIVE:.*]] = arith.cmpi ne
// CHECK: %[[ANTE_VALUE:.*]] = simulation.assert.sampled_read
// CHECK: %[[ANTE_TRUE:.*]] = simulation.logic.is_true %[[ANTE_VALUE]]
// CHECK: arith.andi %[[ANTE_ACTIVE]], {{.*}} {simulation.implication_antecedent_failure}
// CHECK: %[[ANTE_MATCH:.*]] = arith.andi %[[ANTE_ACTIVE]], %[[ANTE_TRUE]] {simulation.implication_antecedent}
// CHECK: cf.cond_br %[[ANTE_MATCH]]
// CHECK: %[[UPDATED_LOCAL:.*]] = simulation.logic.unary bit_not
// CHECK: simulation.spawn @[[MATCH_CALL]](%arg0, %[[UPDATED_LOCAL]])
// CHECK: simulation.logic.compare eq {{.*}}, %[[UPDATED_LOCAL]]
// The cover form shares the same three-age antecedent and explicit one-tick
// handoff. Its action-silent false result does not require an EOS callback.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.bounded_antecedent_horizon = 3 : i64
// CHECK-SAME: simulation.followed_by_monitor
// CHECK: simulation.suspend.edge posedge
// CHECK: arith.andi {{.*}} {simulation.implication_antecedent}
// CHECK: arith.ori {{.*}}, %c1_i64
// For assert followed-by, both still-live antecedent ages fail vacuously at
// EOS, while a terminal live match starts the nonoverlapped bit-zero handoff.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos.{{.*}}.fail.antecedent_no_match(
// CHECK-SAME: simulation.concurrent_eos_forced_completion
// CHECK-SAME: simulation.concurrent_eos_vacuous
// CHECK: arith.andi {{.*}}, %c16_i64
// CHECK: simulation.spawn @unit_2.$concurrent_eos_report.{{.*}}.fail.antecedent_no_match
// CHECK: arith.andi {{.*}}, %c8_i64
// CHECK: simulation.spawn @unit_2.$concurrent_eos_report.{{.*}}.fail.antecedent_no_match
// CHECK-NOT: simulation.spawn @unit_2.$concurrent_eos_report.{{.*}}.fail.antecedent_no_match
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.bounded_antecedent_horizon = 3 : i64
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.followed_by_monitor
// CHECK: %[[FOLLOW_STATE:.*]] = simulation.ref.alloc
// CHECK: simulation.spawn @unit_2.$concurrent_eos.{{.*}}.fail.antecedent_no_match({{.*}}, %[[FOLLOW_STATE]]
// CHECK: simulation.suspend.edge posedge
// CHECK: arith.andi {{.*}} {simulation.implication_antecedent_failure}
// CHECK: arith.andi {{.*}} {simulation.implication_antecedent}
// CHECK: arith.ori {{.*}}, %c1_i64

module sva_local_multicycle_antecedent(input logic clk, a, b, c, d);
  property p_overlap;
    logic x = a;
    @(posedge clk) (a, x = b) ##1 (b, x = !x, $display("x=%0b", x)) |-> (c == x) ##1 d;
  endproperty
  property p_nonoverlap;
    logic x = a;
    @(posedge clk) (a, x = b) ##2 (b, x = !x) #=# (c == x) ##1 d;
  endproperty
  assert property (p_overlap) $display("overlap pass");
  cover property (p_nonoverlap) $display("nonoverlap hit");
  assert property (p_nonoverlap);
endmodule
