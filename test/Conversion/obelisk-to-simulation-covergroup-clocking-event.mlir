// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/event-list.sv -o %t/event-list.mlir
// RUN: obelisk-opt %t/event-list.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/event-list.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=NOLEGACY
// RUN: obelisk-opt %t/event-list.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t/event-list.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --mlir-disable-threading -o %t/serial.mlir
// RUN: obelisk-opt %t/event-list.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   -o %t/threaded.mlir
// RUN: diff -u %t/serial.mlir %t/threaded.mlir
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/no-overcapture.sv -o %t/no-overcapture.mlir
// RUN: obelisk-opt %t/no-overcapture.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=CAPTURE
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/vector-edge.sv -o %t/vector-edge.mlir
// RUN: obelisk-opt %t/vector-edge.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=VECTOR
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/computed.sv -o %t/computed.mlir
// RUN: obelisk-opt %t/computed.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=COMPUTED
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/ref-formal.sv -o %t/ref-formal.mlir
// RUN: obelisk-opt %t/ref-formal.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=REF
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/dynamic-sample-ref.sv -o %t/dynamic-sample-ref.mlir
// RUN: obelisk-opt %t/dynamic-sample-ref.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=DYNAMIC-REF
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/embedded-class.sv -o %t/embedded-class.mlir
// RUN: obelisk-opt %t/embedded-class.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=EMBEDDED

// The detached owner is primed synchronously with new(), registers a compiled
// sample evaluator, and then parks forever. The evaluator runs at each atomic
// publication instead of observing live values from a later scheduler drain.
// SIM: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// SIM-SAME: entry_kind = 14
// SIM: obelisk_sim.covergroup.sample
// SIM: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}
// SIM-SAME: schedule.covergroup_clocking_sampler
// SIM-SAME: schedule.prime_on_spawn
// SIM: obelisk_sim.observer.bind @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// SIM: obelisk_sim.covergroup.clock_event.register
// SIM-SAME: conditions 1 edges [1, 2] indices [0, -1]
// SIM: obelisk_sim.suspend.forever
// SIM: obelisk_sim.covergroup.create
// SIM-NEXT: {{.*}} = obelisk_sim.spawn @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}

// NOLEGACY: obelisk_sim.design
// NOLEGACY-NOT: obelisk_sim.suspend.clock_set
// NOLEGACY-NOT: obelisk_sim.assert.clock_occurrence.consume

// Both event primaries are v1 SamplingEvent expressions. Their physical result
// ordinals are local table details and never define merge identity.
// SCHEMA-COUNT-2: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=1 role=1 result_kind=1 width=0 signedness=3 {{.*}} phase=3

// The parent fork needs its 37-bit automatic local, but the detached coverage
// sampler does not. Do not extend that unrelated reference's lifetime.
// CAPTURE-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}
// CAPTURE-NOT: !obelisk_sim.logic<37>
// CAPTURE-SAME: attributes

// IEEE 1800-2017/2023 9.4.2 detects an explicit edge on the expression's LSB,
// but change events compare the complete packed result. Preserve the full
// two-state/four-state value in the initial plane and observer result.
// VECTOR-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// VECTOR: obelisk_sim.ref.load {{%.*}} : !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 0 x i1>> -> !obelisk_sim.packed_array<3 : 0 x i1>
// VECTOR: obelisk_sim.packed.flatten {{%.*}} : (!obelisk_sim.packed_array<3 : 0 x i1>) -> i4
// VECTOR: obelisk_sim.observer.bind @{{[^ ]+}} values({{.*}}) captures 1 : <i4>
// VECTOR: obelisk_sim.covergroup.clock_event.register
// VECTOR-SAME: edges [1]
// VECTOR-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// VECTOR: obelisk_sim.ref.load {{%.*}} : !obelisk_sim.ref<!obelisk_sim.packed_array<0 : 3 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<0 : 3 x !obelisk_sim.logic<1>>
// VECTOR: obelisk_sim.packed.flatten {{%.*}} : (!obelisk_sim.packed_array<0 : 3 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<4>
// VECTOR: obelisk_sim.observer.bind @{{[^ ]+}} values({{.*}}) captures 1 : <!obelisk_sim.logic<4>>
// VECTOR: obelisk_sim.covergroup.clock_event.register
// VECTOR-SAME: edges [2]
// VECTOR-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// VECTOR: obelisk_sim.net.read {{%.*}} : !obelisk_sim.net<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
// VECTOR: obelisk_sim.packed.flatten {{%.*}} : (!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<8>
// VECTOR: obelisk_sim.observer.bind @{{[^ ]+}} values({{.*}}) captures 1 : <!obelisk_sim.logic<8>>
// VECTOR: obelisk_sim.covergroup.clock_event.register
// VECTOR-SAME: edges [3]

// A computed primary and its iff are independent compiled observers. The
// construction-time value initializes the same full-width v1 event plan.
// COMPUTED-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// COMPUTED: [[A:%.*]] = obelisk_sim.ref.load
// COMPUTED: [[B:%.*]] = obelisk_sim.ref.load
// COMPUTED: [[INITIAL:%.*]] = arith.xori [[A]], [[B]] : i1
// COMPUTED: [[PRIMARY:%.*]] = obelisk_sim.observer.bind @[[PRIMARY_FN:[^ ]+]] values({{.*}}) captures 2 : <i1>
// COMPUTED: [[IFF:%.*]] = obelisk_sim.observer.bind @[[IFF_FN:[^ ]+]] values({{.*}}) captures 1 : <i1>
// COMPUTED: [[SAMPLER:%.*]] = obelisk_sim.observer.bind @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// COMPUTED: obelisk_sim.covergroup.clock_event.register {{.*}} events{{\[}}[[PRIMARY]], [[INITIAL]], [[IFF]], [[SAMPLER]]] conditions 1 edges [1] indices [0]
// COMPUTED: obelisk_sim.func private @[[PRIMARY_FN]](
// COMPUTED: arith.xori
// COMPUTED: obelisk_sim.func private @[[IFF_FN]](

// A constructor ref formal is retained as one first-class ArgumentRef in the
// event evaluator and sampler. Each new() captures its actual alias, including
// a managed class field, instead of freezing the construction-time value.
// REF-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// REF-SAME: %[[EVENT_REF:[^:]+]]: !obelisk_sim.argument_ref<i1>
// REF: obelisk_sim.argument_ref.load %[[EVENT_REF]]
// REF: obelisk_sim.observer.bind @[[REF_EVALUATOR:[^ ]+]] values(%[[EVENT_REF]], %[[EVENT_REF]] : !obelisk_sim.argument_ref<i1>, !obelisk_sim.argument_ref<i1>) captures 1 : <i1>
// REF: obelisk_sim.covergroup.clock_event.register
// REF: obelisk_sim.argument_ref.from_ref
// REF: obelisk_sim.argument_ref.from_managed
// REF: obelisk_sim.func private @[[REF_EVALUATOR]](
// REF-SAME: !obelisk_sim.argument_ref<i1>
// REF: obelisk_sim.argument_ref.load

// A dynamic reference path used by the sample expression or iff remains a
// value capture. It is not a primary dependency and therefore does not need a
// dynamic scheduler watch.
// DYNAMIC-REF: obelisk_sim.observer.bind
// DYNAMIC-REF: obelisk_sim.covergroup.clock_event.register
// DYNAMIC-REF: obelisk_sim.argument_ref.from_path

// IEEE 1800-2023 19.4 permits an embedded class covergroup to use properties
// of its containing object in both the sampling event and coverpoints. Retain
// that object in the detached sampler, and attach the field watch to the
// primary observer so each instance observes only its own clock.
// EMBEDDED-LABEL: obelisk_sim.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EMBEDDED-SAME: %[[THIS:[^:]+]]: !obelisk_sim.class_handle<@[[CLASS:[^>]+]]>
// EMBEDDED: %[[CLOCK_REF:.*]] = obelisk_sim.class.field_ref %[[THIS]][@{{[^ ]+}}] : !obelisk_sim.class_handle<@[[CLASS]]> -> !obelisk_sim.managed_ref<i1, @[[CLASS]]>
// EMBEDDED: %[[WATCH:.*]] = obelisk_sim.managed.watch field %[[CLOCK_REF]]
// EMBEDDED: %[[PRIMARY:.*]] = obelisk_sim.observer.bind @{{[^ ]+}} values(%[[THIS]], %[[WATCH]] : !obelisk_sim.class_handle<@[[CLASS]]>, !obelisk_sim.managed_watch) captures 1 : <i1>
// EMBEDDED: %[[SAMPLE:.*]] = obelisk_sim.observer.bind @{{[^ ]+}} values(%{{[^,]+}}, %[[THIS]] : !obelisk_sim.covergroup_handle<{{[^>]+}}>, !obelisk_sim.class_handle<@[[CLASS]]>) captures 2 : <i1>
// EMBEDDED: obelisk_sim.covergroup.clock_event.register {{.*}} events{{\[}}%[[PRIMARY]], {{%[^,]+}}, %[[SAMPLE]]]
// EMBEDDED: obelisk_sim.spawn @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}({{[^,]+}}, {{[^,]+}}, %{{[^)]+}})

//--- event-list.sv
module top;
  bit clock_a;
  bit clock_b;
  bit gate;
  bit sampled;

  covergroup clocked @(posedge clock_a iff gate or negedge clock_b);
    point: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  clocked coverage;
  initial coverage = new;
endmodule

//--- no-overcapture.sv
module no_overcapture;
  bit clock;
  bit sampled;
  covergroup cg @(posedge clock);
    point: coverpoint sampled;
  endgroup
  cg coverage;

  initial begin
    automatic bit [36:0] unrelated = '0;
    fork
      begin
        coverage = new;
        unrelated = '1;
      end
    join
  end
endmodule

//--- vector-edge.sv
module vector_edge;
  bit [3:0] descending_clock;
  logic [0:3] ascending_clock;
  wire logic [7:0] both_clock;
  bit sampled;

  covergroup descending @(posedge descending_clock);
    point: coverpoint sampled;
  endgroup

  covergroup ascending @(negedge ascending_clock);
    point: coverpoint sampled;
  endgroup

  covergroup both_edges @(edge both_clock);
    point: coverpoint sampled;
  endgroup

  descending descending_coverage;
  ascending ascending_coverage;
  both_edges both_coverage;
  initial begin
    descending_coverage = new;
    ascending_coverage = new;
    both_coverage = new;
  end
endmodule

//--- computed.sv
module computed;
  bit clock_a;
  bit clock_b;
  bit gate = 1;

  covergroup cg @(posedge (clock_a ^ clock_b) iff gate);
    point: coverpoint clock_a;
  endgroup

  cg coverage = new;
endmodule

//--- ref-formal.sv
module ref_formal;
  class holder;
    bit clock;
  endclass

  bit clock;
  bit sampled;
  holder object = new;

  covergroup cg(ref bit event_clock) @(posedge event_clock);
    point: coverpoint sampled;
  endgroup

  cg direct = new(clock);
cg member = new(object.clock);
endmodule

//--- dynamic-sample-ref.sv
module dynamic_sample_ref;
  bit clock;
  bit samples[$] = '{0, 1};
  int selected;

  covergroup cg(ref bit sampled) @(posedge clock iff sampled);
    point: coverpoint sampled;
  endgroup

cg coverage = new(samples[selected]);
endmodule

//--- embedded-class.sv
module embedded_class;
  class monitor;
    bit clock;
    bit sampled;

    covergroup cg @(posedge clock);
      point: coverpoint sampled;
    endgroup

    function new;
      cg = new;
    endfunction
  endclass

  monitor object = new;
endmodule
