// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/block.sv -o %t/block.mlir
// RUN: obelisk-opt %t/block.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=BLOCK
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/reference-path.sv -o %t/reference-path.mlir
// RUN: not obelisk-opt %t/reference-path.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REFERENCE-PATH
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/publishing-call.sv -o %t/publishing-call.mlir
// RUN: not obelisk-opt %t/publishing-call.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PUBLISHING-CALL
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/publishing-method.sv -o %t/publishing-method.mlir
// RUN: not obelisk-opt %t/publishing-method.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PUBLISHING-METHOD
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/publishing-ref-call.sv -o %t/publishing-ref-call.mlir
// RUN: not obelisk-opt %t/publishing-ref-call.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PUBLISHING-REF-CALL
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/method-read.sv -o %t/method-read.mlir
// RUN: not obelisk-opt %t/method-read.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=METHOD-READ

// Block events register one synchronous sample observer at construction and
// instrument the target definition. They do not create a parked process, and
// the single mutable v1 path has no compatibility operation.
// BLOCK-COUNT-2: simulation.covergroup.block_event.fire
// BLOCK-DAG: simulation.covergroup.block_event.register
// BLOCK-DAG: simulation.covergroup_block_event_sample_evaluator
// BLOCK-NOT: schedule.covergroup_clocking_sampler

// REFERENCE-PATH: clocking-event covergroup constructor ref formal
// REFERENCE-PATH-SAME: is bound through a dynamic reference path

// PUBLISHING-CALL: covergroup clocking-event primary and iff calls must be transitively read-only
// PUBLISHING-METHOD: covergroup clocking-event primary and iff calls must be transitively read-only
// PUBLISHING-REF-CALL: covergroup clocking-event primary and iff calls must be transitively read-only
// METHOD-READ: covergroup clocking-event primary and iff calls must be transitively read-only with statically materializable dependencies

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

//--- publishing-call.sv
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

//--- publishing-method.sv
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

//--- publishing-ref-call.sv
module publishing_ref_call;
  bit clock;
  bit side_effect;

  function automatic void mutate(ref bit destination);
    destination = !destination;
  endfunction

  function bit read_and_write_clock;
    mutate(side_effect);
    return clock;
  endfunction

  covergroup cg @(posedge read_and_write_clock());
    point: coverpoint side_effect;
  endgroup

  cg coverage = new;
endmodule

//--- method-read.sv
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
