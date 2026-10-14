// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/basic.sv \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=BASIC
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/basic.sv \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=TARGET
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/basic.sv \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   | FileCheck %s --check-prefix=TARGET
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/basic.sv \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --test-obelisk-print-coverage-schema -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %obelisk --std=1800-2017 -emit-obelisk %t/basic.sv \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=BASIC
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/others.sv \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=OTHERS

// IEEE 1800-2017/2023 19.3 block events sample synchronously at a target's
// begin/end boundary.  They neither park a process nor honor strobe.
// BASIC: simulation.func private @{{[^ (]*}}covergroup_block_event_sample
// BASIC-SAME: entry_kind = 14
// BASIC-SAME: simulation.covergroup_block_event_sample_evaluator
// BASIC: simulation.covergroup.sample
// BASIC: simulation.observer.bind @{{.*}}covergroup_block_event_sample
// BASIC: simulation.covergroup.block_event.register
// BASIC-SAME: event_kinds = array<i32: 0, 1>
// BASIC-NOT: simulation.covergroup.block_event.plan
// BASIC-NOT: schedule.covergroup_clocking_sampler
// BASIC-NOT: simulation.suspend.forever

// The target definition carries both boundaries at O0 and O3.  The runtime
// registry decides which registered clauses consume each one.
// TARGET-LABEL: simulation.func private @{{[^ (]+}}(
// TARGET-SAME: simulation.hierarchical_name = "basic.target"
// TARGET: simulation.covergroup.block_event.fire
// TARGET-SAME: event_kind = #simulation.block_event_kind<begin>
// TARGET: simulation.ref.store
// TARGET: simulation.covergroup.block_event.fire
// TARGET-SAME: event_kind = #simulation.block_event_kind<end>
// TARGET: simulation.return

// Both schema expressions are event-phase SamplingEvent entries.  Block is
// flag 2 and block-end is flag 2|4.  Strobe does not alter either entry.
// SCHEMA-DAG: functional_expression {{.*}} role=1 {{.*}} flags=2
// SCHEMA-DAG: functional_expression {{.*}} role=1 {{.*}} flags=6

// An embedded covergroup captures its owning object.  Registration and the
// instrumented method carry that same receiver, and the evaluator receives it
// as argument 2 for member-valued coverpoints.
// OTHERS-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.hierarchical_name = "holder::target"
// OTHERS-COUNT-2: simulation.covergroup.block_event.fire %{{[^,]+}}, %{{[^ ]+}}
// OTHERS: simulation.func private @{{[^ (]*}}covergroup_block_event_sample
// OTHERS-SAME: %{{[^:]+}}: !simulation.class_handle<
// OTHERS-SAME: simulation.covergroup_block_event_sample_evaluator
// OTHERS: simulation.observer.bind @{{[^ ]*}}covergroup_block_event_sample
// OTHERS-SAME: values(%{{[^,]+}}, %{{[^ ]+}} : !simulation.covergroup_handle<
// OTHERS-SAME: !simulation.class_handle<
// OTHERS-SAME: captures 2
// OTHERS: simulation.covergroup.block_event.register %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}}

// An embedded covergroup still captures its class owner for sampling a static
// method target, but the event registration and firing have no receiver filter.
// OTHERS-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.hierarchical_name = "static_holder::target"
// OTHERS-COUNT-2: simulation.covergroup.block_event.fire %{{[^ ]+}} {
// OTHERS: simulation.func private @{{[^ (]*}}covergroup_block_event_sample
// OTHERS-SAME: %{{[^:]+}}: !simulation.class_handle<
// OTHERS: simulation.covergroup.block_event.register %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}}
// OTHERS-SAME: event_kinds = array<i32: 0, 1>

// A subroutine initializes its leading block-item declarations before its
// first statement. The begin sample must therefore follow a declaration
// initializer call whose side effects can be observed by the covergroup.
// OTHERS-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.hierarchical_name = "declaration_case.target"
// OTHERS: simulation.call
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<begin>
// OTHERS: simulation.ref.store
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<end>

// `disable watched` branches directly to the named block's exit.  Therefore
// only the begin fire remains reachable in this procedure.
// OTHERS-LABEL: simulation.code_unit.decl {{.*}} hierarchy "disable_case.
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<begin>
// OTHERS: simulation.control.disable
// OTHERS-NEXT: cf.br

// A named parallel block begins before its children spawn and ends only after
// the requested join completes.
// OTHERS-LABEL: simulation.code_unit.decl {{.*}} hierarchy "fork_case.
// OTHERS: simulation.control.enter
// OTHERS-NEXT: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<begin>
// OTHERS: simulation.suspend.join all
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<end>
// OTHERS-NEXT: simulation.control.leave

// Functions use the same exact boundaries, including an explicit return.
// OTHERS-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.hierarchical_name = "function_case.target"
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<begin>
// OTHERS: simulation.ref.store
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<end>
// OTHERS-NEXT: simulation.return

// Task output copy-out happens after the end boundary. The assignment to the
// task-local formal is a statement; publishing its value to the caller is not.
// OTHERS-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.hierarchical_name = "task_output_case.target"
// OTHERS: simulation.ref.store
// OTHERS: simulation.covergroup.block_event.fire
// OTHERS-SAME: event_kind = #simulation.block_event_kind<end>
// OTHERS: simulation.ref.load
// OTHERS: simulation.ref.store
// OTHERS: simulation.return

//--- basic.sv
module basic;
  int sampled;

  task automatic target;
    sampled++;
  endtask

  covergroup cg @@(begin target or end target);
    type_option.strobe = 1;
    point: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg coverage;
  initial begin
    coverage = new;
    target();
  end
endmodule


//--- others.sv
class holder;
  int sampled;

  task target;
    sampled++;
  endtask

  covergroup cg @@(begin target or end target);
    point: coverpoint sampled;
  endgroup

  function new;
    cg = new;
  endfunction
endclass

module class_case;
  holder first;
  holder second;
  initial begin
    first = new;
    second = new;
    first.target();
    second.target();
  end
endmodule

class static_holder;
  static task target;
    $display("target");
  endtask

  covergroup cg @@(begin target or end target);
    point: coverpoint 1;
  endgroup

  function new;
    cg = new;
  endfunction
endclass

module static_case;
  static_holder object;
  initial begin
    object = new;
    static_holder::target();
  end
endmodule

module declaration_case;
  int sampled;

  function int initialize;
    sampled = 7;
    return 1;
  endfunction

  task automatic target;
    int temporary = initialize();
    sampled += temporary;
  endtask

  covergroup cg @@(begin target or end target);
    point: coverpoint sampled;
  endgroup

  cg coverage;
  initial begin
    coverage = new;
    target();
  end
endmodule

module disable_case;
  int sampled;

  covergroup cg @@(begin watched or end watched);
    point: coverpoint sampled;
  endgroup

  cg coverage;
  initial begin
    coverage = new;
    begin : watched
      sampled = 1;
      disable watched;
      sampled = 2;
    end
  end
endmodule

module fork_case;
  int sampled;

  covergroup cg @@(begin watched or end watched);
    point: coverpoint sampled;
  endgroup

  cg coverage;
  initial begin
    coverage = new;
    fork : watched
      sampled = 1;
      sampled = 2;
    join
  end
endmodule

module function_case;
  int sampled;

  function int target;
    sampled = 1;
    return sampled;
  endfunction

  covergroup cg @@(begin target or end target);
    point: coverpoint sampled;
  endgroup

  cg coverage;
  initial begin
    coverage = new;
    sampled = target();
  end
endmodule

module task_output_case;
  int actual;

  task target(output int value);
    value = 7;
  endtask

  covergroup cg @@(end target);
    point: coverpoint actual;
  endgroup

  cg coverage;
  initial begin
    coverage = new;
    target(actual);
  end
endmodule
