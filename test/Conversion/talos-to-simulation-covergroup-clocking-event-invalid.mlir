// RUN: %split-file %s %t
// RUN: %talos --std=1800-2023 -emit-talos %t/block.sv \
// RUN:   | talos-opt '--lower-talos-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=BLOCK
// RUN: %talos --std=1800-2023 -emit-talos %t/reference-path.sv \
// RUN:   | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REFERENCE-PATH
// RUN: %talos --std=1800-2023 -emit-talos %t/errors.sv \
// RUN:   | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERROR

// Block events register one synchronous sample observer at construction and
// instrument the target definition. They do not create a parked process, and
// the single mutable path has no compatibility operation.
// BLOCK-COUNT-2: simulation.covergroup.block_event.fire
// BLOCK-DAG: simulation.covergroup.block_event.register
// BLOCK-DAG: simulation.covergroup_block_event_sample_evaluator
// BLOCK-NOT: schedule.covergroup_clocking_sampler

// REFERENCE-PATH: clocking-event covergroup constructor ref formal
// REFERENCE-PATH-SAME: is bound through a dynamic reference path

// Every clocking-event primary or iff call must be transitively read-only:
// a direct write, a write through a method receiver, a write through a ref
// argument, and a method receiver read are each rejected at their covergroup.
// ERROR-DAG: error: covergroup clocking-event primary and iff calls must be transitively read-only{{([^@]|[[:space:]])*}}@(posedge read_and_write_clock());
// ERROR-DAG: error: covergroup clocking-event primary and iff calls must be transitively read-only{{([^@]|[[:space:]])*}}@(posedge object.read_and_write_clock());
// ERROR-DAG: error: covergroup clocking-event primary and iff calls must be transitively read-only{{([^@]|[[:space:]])*}}@(posedge mutate_and_read_clock());
// ERROR-DAG: error: covergroup clocking-event primary and iff calls must be transitively read-only with statically materializable dependencies; {{([^@]|[[:space:]])*}}@(posedge object.read_clock());

//--- block.sv
module block_event;
  bit sampled;
  task automatic work;
    sampled = !sampled;
  endtask
  covergroup cg @@ (begin work);
    point: coverpoint sampled;
  endgroup
  cg coverage;
  initial begin
    coverage = new;
    work();
  end
endmodule

//--- reference-path.sv
module reference_path_event;
  bit clocks[$] = '{0, 0};
  int selected;

  covergroup cg(ref bit event_clock) @(posedge event_clock);
    point: coverpoint selected;
  endgroup

cg coverage = new(clocks[selected]);
endmodule

//--- errors.sv
module publishing_call;
  bit clock;
  bit side_effect;

  function bit read_and_write_clock;
    side_effect = !side_effect;
    return clock;
  endfunction

  covergroup cg @(posedge read_and_write_clock());
    point: coverpoint side_effect;
  endgroup

cg coverage = new;
endmodule

module publishing_method;
  class holder;
    bit clock;
    bit side_effect;

    function bit read_and_write_clock;
      side_effect = !side_effect;
      return clock;
    endfunction
  endclass

  holder object = new;
  covergroup cg @(posedge object.read_and_write_clock());
    point: coverpoint object.side_effect;
  endgroup

  cg coverage = new;
endmodule

module publishing_ref_call;
  bit clock;
  bit side_effect;

  function automatic void mutate(ref bit destination);
    destination = !destination;
  endfunction

  function bit mutate_and_read_clock;
    mutate(side_effect);
    return clock;
  endfunction

  covergroup cg @(posedge mutate_and_read_clock());
    point: coverpoint side_effect;
  endgroup

  cg coverage = new;
endmodule

module method_read;
  class holder;
    bit clock;

    function bit read_clock;
      return clock;
    endfunction
  endclass

  holder object = new;
  covergroup cg @(posedge object.read_clock());
    point: coverpoint object.clock;
  endgroup

  cg coverage = new;
endmodule
