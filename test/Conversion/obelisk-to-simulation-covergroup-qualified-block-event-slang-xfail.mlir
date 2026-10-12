// XFAIL: *
// IEEE 1800-2023 19.3 permits class methods in block event expressions.
// Slang v11.0 rejects the object-qualified method before AST import.

// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/qualified.sv -o %t/qualified.mlir
// RUN: obelisk-opt %t/qualified.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=QUALIFIED

// Object-qualified instance methods retain one receiver per event clause.
// Different receivers become distinct registrations while sharing the same
// target identity and synchronous firing points.
// QUALIFIED-COUNT-2: simulation.func private @{{[^ (]*}}covergroup_block_event_sample
// QUALIFIED: simulation.observer.bind @{{.*}} captures 2 : !simulation.observer<i1>
// QUALIFIED: simulation.covergroup.block_event.register %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}}
// QUALIFIED-SAME: event_kinds = array<i32: 0>
// QUALIFIED: simulation.observer.bind @{{.*}} captures 2 : !simulation.observer<i1>
// QUALIFIED: simulation.covergroup.block_event.register %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}}
// QUALIFIED-SAME: event_kinds = array<i32: 1>

//--- qualified.sv
class qualified_holder;
  int sampled;

  task target;
    sampled++;
  endtask
endclass

module qualified_case;
  qualified_holder first;
  qualified_holder second;

  covergroup cg @@(begin first.target or end second.target);
    point: coverpoint first.sampled;
  endgroup

  cg coverage;
  initial begin
    first = new;
    second = new;
    coverage = new;
    first.target();
    second.target();
  end
endmodule
