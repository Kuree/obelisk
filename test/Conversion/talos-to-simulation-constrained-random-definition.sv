// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: %if z3 %{ talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' -o %t %}
// RUN: %if z3 %{ FileCheck %s --check-prefix=DEFINITION < %t %}
// RUN: %if z3 %{ FileCheck %s --check-prefix=COMPOSE-FALLBACK < %t %}
// RUN: %if !z3 %{ talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=DEFINITION-FALLBACK %}

// DEFINITION-LABEL: simulation.func private @unit_1
// DEFINITION: %[[RAW:.*]] = arith.andi {{.*}}, {{.*}} : i64
// DEFINITION: ^bb{{[0-9]+}}(%{{.*}}: i64, %{{.*}}: i64, %{{.*}}: i64):
// Z3 can remove the nested division/modulus/power comparison through complete
// assignment tables or constrained domains. Check the remaining materialized
// packing definitions without requiring arithmetic that compilation proved
// unnecessary. Runtime arithmetic semantics have separate MLIR coverage.
// The copy alias is fed from the already materialized x definition. The flag
// definition above reads that same canonical alias representative.
// DEFINITION-DAG: arith.shli {{.*}}, {{.*}} : i64
// Packed concatenation and constant replication are expressed through the
// existing total fixed-width definition operators. Replication uses the
// geometric bit-placement multiplier (1 + 2^2 = 5).
// DEFINITION-DAG: arith.shli {{.*}}, %{{c2_i64.*}} : i64
// DEFINITION-DAG: arith.ori
// DEFINITION-DAG: arith.muli {{.*}}, %{{c5_i64.*}} : i64
// Both retry attempts remain available before committing the materialized
// definition proposal.
// DEFINITION-COUNT-2: simulation.random.solve {{.*}} mutable
// DEFINITION: simulation.managed.store
// DEFINITION: simulation.managed.store
// DEFINITION: simulation.managed.store
// DEFINITION: simulation.managed.store
// A definition target whose inferred domain is narrower than its bit width must
// retain the checker and runtime fallback: overwriting the sampled domain with
// the definition alone does not prove that their conjunction holds.
// COMPOSE-FALLBACK-LABEL: simulation.func private @unit_1
// COMPOSE-FALLBACK: arith.xori
// COMPOSE-FALLBACK: arith.cmpi ne
// COMPOSE-FALLBACK: arith.cmpi ne
// COMPOSE-FALLBACK: arith.andi
// COMPOSE-FALLBACK: arith.extui
// COMPOSE-FALLBACK: arith.cmpi eq
// COMPOSE-FALLBACK: simulation.random.solve
// DEFINITION-FALLBACK-LABEL: simulation.func private @unit_1
// DEFINITION-FALLBACK: arith.addi
// DEFINITION-FALLBACK: arith.cmpi eq
// DEFINITION-FALLBACK: simulation.random.solve

class C;
  rand bit [1:0] x, y, z;
  rand bit flag;
  rand bit [1:0] pick;
  rand bit op;
  rand bit [1:0] copy;
  rand bit [3:0] cat, rep;
  constraint defined {
    x == y + 2'd1;
    y == z + 2'd1;
    flag == (copy < z);
    pick == (flag &&& op ? y : z);
    op == (((x / 2'd3) << z) % 2'd3 == ((y ** 2'd3) >> z));
    copy == x;
    cat == {x, y};
    rep == {2{x}};
  }
endclass
module top;
  C object = new;
  int result;
  initial begin
    result = object.randomize();
    void'(object.randomize() with { x < 2'd2; op == (x < z && ^2'd1); });
  end
endmodule
