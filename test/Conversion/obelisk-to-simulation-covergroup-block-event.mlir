// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/basic.sv -o %t/basic.mlir
// RUN: obelisk-opt %t/basic.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=BASIC
// RUN: obelisk-opt %t/basic.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=TARGET
// RUN: obelisk-opt %t/basic.mlir '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   | FileCheck %s --check-prefix=TARGET
// RUN: obelisk-opt %t/basic.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/class.sv -o %t/class.mlir
// RUN: obelisk-opt %t/class.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=CLASS
// RUN: obelisk-opt %t/class.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=CLASS-FIRE
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/disable.sv -o %t/disable.mlir
// RUN: obelisk-opt %t/disable.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=DISABLE
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/declaration.sv -o %t/declaration.mlir
// RUN: obelisk-opt %t/declaration.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=DECL
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/function.sv -o %t/function.mlir
// RUN: obelisk-opt %t/function.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=FUNCTION
// RUN: %obelisk --std=1800-2017 -emit-obelisk %t/basic.sv -o %t/basic-2017.mlir
// RUN: obelisk-opt %t/basic-2017.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=BASIC
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/qualified.sv -o %t/qualified.mlir
// RUN: obelisk-opt %t/qualified.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=QUALIFIED
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/fork.sv -o %t/fork.mlir
// RUN: obelisk-opt %t/fork.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=FORK
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/static.sv -o %t/static.mlir
// RUN: obelisk-opt %t/static.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=STATIC
// RUN: obelisk-opt %t/static.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=STATIC-FIRE
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/task-output.sv -o %t/task-output.mlir
// RUN: obelisk-opt %t/task-output.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=TASK-OUTPUT

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
// CLASS: simulation.func private @{{[^ (]*}}covergroup_block_event_sample
// CLASS-SAME: %{{[^:]+}}: !simulation.class_handle<
// CLASS-SAME: simulation.covergroup_block_event_sample_evaluator
// CLASS: simulation.observer.bind @{{[^ ]*}}covergroup_block_event_sample
// CLASS-SAME: values(%{{[^,]+}}, %{{[^ ]+}} : !simulation.covergroup_handle<
// CLASS-SAME: !simulation.class_handle<
// CLASS-SAME: captures 2
// CLASS: simulation.covergroup.block_event.register %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}}
// CLASS-FIRE-COUNT-2: simulation.covergroup.block_event.fire %{{[^,]+}}, %{{[^ ]+}}

// `disable watched` branches directly to the named block's exit.  Therefore
// only the begin fire remains reachable in this procedure.
// DISABLE-COUNT-1: simulation.covergroup.block_event.fire
// DISABLE-SAME: event_kind = #simulation.block_event_kind<begin>
// DISABLE: simulation.control.disable
// DISABLE-NEXT: cf.br

// A subroutine initializes its leading block-item declarations before its
// first statement. The begin sample must therefore follow a declaration
// initializer call whose side effects can be observed by the covergroup.
// DECL-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.coverage_block_event_target_id
// DECL-SAME: simulation.hierarchical_name = "declaration_case.target"
// DECL: simulation.call
// DECL: simulation.covergroup.block_event.fire
// DECL-SAME: event_kind = #simulation.block_event_kind<begin>
// DECL: simulation.ref.store
// DECL: simulation.covergroup.block_event.fire
// DECL-SAME: event_kind = #simulation.block_event_kind<end>

// Functions use the same exact boundaries, including an explicit return.
// FUNCTION-LABEL: simulation.func private @{{[^ (]+}}({{.*}}simulation.coverage_block_event_target_id
// FUNCTION-SAME: simulation.hierarchical_name = "function_case.target"
// FUNCTION: simulation.covergroup.block_event.fire
// FUNCTION-SAME: event_kind = #simulation.block_event_kind<begin>
// FUNCTION: simulation.ref.store
// FUNCTION: simulation.covergroup.block_event.fire
// FUNCTION-SAME: event_kind = #simulation.block_event_kind<end>
// FUNCTION-NEXT: simulation.return

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

// A named parallel block begins before its children spawn and ends only after
// the requested join completes.
// FORK: simulation.control.enter
// FORK-NEXT: simulation.covergroup.block_event.fire
// FORK-SAME: event_kind = #simulation.block_event_kind<begin>
// FORK: simulation.suspend.join all
// FORK: simulation.covergroup.block_event.fire
// FORK-SAME: event_kind = #simulation.block_event_kind<end>
// FORK-NEXT: simulation.control.leave

// An embedded covergroup still captures its class owner for sampling a static
// method target, but the event registration and firing have no receiver filter.
// STATIC: simulation.func private @{{[^ (]*}}covergroup_block_event_sample
// STATIC-SAME: %{{[^:]+}}: !simulation.class_handle<
// STATIC: simulation.covergroup.block_event.register %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}}
// STATIC-SAME: event_kinds = array<i32: 0, 1>
// STATIC-FIRE-COUNT-2: simulation.covergroup.block_event.fire %{{[^ ]+}} {

// Task output copy-out happens after the end boundary. The assignment to the
// task-local formal is a statement; publishing its value to the caller is not.
// TASK-OUTPUT-LABEL: simulation.func private @{{[^ (]+}}(
// TASK-OUTPUT-SAME: simulation.hierarchical_name = "task_output_case.target"
// TASK-OUTPUT: simulation.ref.store
// TASK-OUTPUT: simulation.covergroup.block_event.fire
// TASK-OUTPUT-SAME: event_kind = #simulation.block_event_kind<end>
// TASK-OUTPUT: simulation.ref.load
// TASK-OUTPUT: simulation.ref.store
// TASK-OUTPUT: simulation.return

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

//--- class.sv
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

//--- disable.sv
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

//--- declaration.sv
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

//--- function.sv
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

//--- fork.sv
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

//--- static.sv
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

//--- task-output.sv
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
