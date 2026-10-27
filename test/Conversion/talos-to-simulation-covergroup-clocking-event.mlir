// RUN: %split-file %s %t
// RUN: %talos --std=1800-2023 -emit-talos %t/event-list.sv \
// RUN:   | talos-opt '--lower-talos-to-sim=opt-level=0' \
// RUN:       --mlir-disable-threading -o %t/serial.mlir
// RUN: %talos --std=1800-2023 -emit-talos %t/event-list.sv \
// RUN:   | talos-opt '--lower-talos-to-sim=opt-level=0' -o %t/threaded.mlir
// RUN: diff -u %t/serial.mlir %t/threaded.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/threaded.mlir
// RUN: FileCheck %s --check-prefix=NOLEGACY < %t/threaded.mlir
// RUN: talos-opt %t/threaded.mlir --test-talos-print-coverage-schema \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: %talos --std=1800-2023 -emit-talos %t/others.sv \
// RUN:   | talos-opt '--lower-talos-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=EVENT

// The detached owner is primed synchronously with new(), registers a compiled
// sample evaluator, and then parks forever. The evaluator runs at each atomic
// publication instead of observing live values from a later scheduler drain.
// SIM: simulation.func private @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// SIM-SAME: entry_kind = 14
// SIM: simulation.covergroup.sample
// SIM: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}
// SIM-SAME: schedule.covergroup_clocking_sampler
// SIM-SAME: schedule.prime_on_spawn
// SIM: simulation.observer.bind @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// SIM: simulation.covergroup.clock_event.register
// SIM-SAME: conditions 1 edges [1, 2] indices [0, -1]
// SIM: simulation.suspend.forever
// SIM: simulation.covergroup.create
// SIM-NEXT: {{.*}} = simulation.spawn @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}

// NOLEGACY: simulation.design
// NOLEGACY-NOT: simulation.suspend.clock_set
// NOLEGACY-NOT: simulation.assert.clock_occurrence.consume

// Both event primaries are v1 SamplingEvent expressions. Their physical result
// ordinals are local table details and never define merge identity.
// SCHEMA-COUNT-2: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=1 role=1 result_kind=1 width=0 signedness=3 {{.*}} phase=3

// A computed primary and its iff are independent compiled observers. The
// construction-time value initializes the same full-width v1 event plan.
// EVENT-LABEL: simulation.code_unit.decl {{.*}} fork hierarchy "computed.{{[^"]*}}$covergroup_event.
// EVENT: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT: [[A:%.*]] = simulation.ref.load
// EVENT: [[B:%.*]] = simulation.ref.load
// EVENT: [[INITIAL:%.*]] = arith.xori [[A]], [[B]] : i1
// EVENT: [[PRIMARY:%.*]] = simulation.observer.bind @[[PRIMARY_FN:[^ ]+]] values({{.*}}) captures 2 : !simulation.observer<i1>
// EVENT: [[IFF:%.*]] = simulation.observer.bind @[[IFF_FN:[^ ]+]] values({{.*}}) captures 1 : !simulation.observer<i1>
// EVENT: [[SAMPLER:%.*]] = simulation.observer.bind @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// EVENT: simulation.covergroup.clock_event.register {{.*}} events{{\[}}[[PRIMARY]], [[INITIAL]], [[IFF]], [[SAMPLER]]] conditions 1 edges [1] indices [0]

// A dynamic reference path used by the sample expression or iff remains a
// value capture. It is not a primary dependency and therefore does not need a
// dynamic scheduler watch.
// EVENT-LABEL: simulation.code_unit.decl {{.*}} fork hierarchy "dynamic_sample_ref.{{[^"]*}}$covergroup_event.
// EVENT: simulation.observer.bind
// EVENT: simulation.covergroup.clock_event.register
// EVENT: simulation.argument_ref.from_path

// IEEE 1800-2023 19.4 permits an embedded class covergroup to use properties
// of its containing object in both the sampling event and coverpoints. Retain
// that object in the detached sampler, and attach the field watch to the
// primary observer so each instance observes only its own clock.
// EVENT-LABEL: simulation.code_unit.decl {{.*}} fork hierarchy "embedded_class.{{[^"]*}}$covergroup_event.
// EVENT: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT-SAME: %[[THIS:[^:]+]]: !simulation.class_handle<@[[CLASS:[^>]+]]>
// EVENT: %[[CLOCK_REF:.*]] = simulation.class.field_ref %[[THIS]][@{{[^ ]+}}] : !simulation.class_handle<@[[CLASS]]> -> !simulation.managed_ref<i1, @[[CLASS]]>
// EVENT: %[[WATCH:.*]] = simulation.managed.watch field %[[CLOCK_REF]]
// EVENT: %[[PRIMARY_E:.*]] = simulation.observer.bind @{{[^ ]+}} values(%[[THIS]], %[[WATCH]] : !simulation.class_handle<@[[CLASS]]>, !simulation.managed_watch) captures 1 : !simulation.observer<i1>
// EVENT: %[[SAMPLE:.*]] = simulation.observer.bind @{{[^ ]+}} values(%{{[^,]+}}, %[[THIS]] : !simulation.covergroup_handle<{{[^>]+}}>, !simulation.class_handle<@[[CLASS]]>) captures 2 : !simulation.observer<i1>
// EVENT: simulation.covergroup.clock_event.register {{.*}} events{{\[}}%[[PRIMARY_E]], {{%[^,]+}}, %[[SAMPLE]]]
// EVENT: simulation.spawn @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}({{[^,]+}}, {{[^,]+}}, %{{[^)]+}})

// The parent fork needs its 37-bit automatic local, but the detached coverage
// sampler does not. Do not extend that unrelated reference's lifetime.
// EVENT-LABEL: simulation.code_unit.decl {{.*}} fork hierarchy "no_overcapture.{{[^"]*}}$covergroup_event.
// EVENT: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT-NOT: !simulation.logic<37>
// EVENT-SAME: attributes

// A constructor ref formal is retained as one first-class ArgumentRef in the
// event evaluator and sampler. Each new() captures its actual alias, including
// a managed class field, instead of freezing the construction-time value.
// EVENT-LABEL: simulation.code_unit.decl {{.*}} fork hierarchy "ref_formal.direct.{{[^"]*}}$covergroup_event.
// EVENT: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT-SAME: %[[EVENT_REF:[^:]+]]: !simulation.argument_ref<i1>
// EVENT: simulation.argument_ref.load %[[EVENT_REF]]
// EVENT: simulation.observer.bind @[[REF_EVALUATOR:[^ ]+]] values(%[[EVENT_REF]], %[[EVENT_REF]] : !simulation.argument_ref<i1>, !simulation.argument_ref<i1>) captures 1 : !simulation.observer<i1>
// EVENT: simulation.covergroup.clock_event.register
// EVENT: simulation.argument_ref.from_ref
// EVENT: simulation.argument_ref.from_managed

// IEEE 1800-2017/2023 9.4.2 detects an explicit edge on the expression's LSB,
// but change events compare the complete packed result. Preserve the full
// two-state/four-state value in the initial plane and observer result.
// EVENT-LABEL: simulation.code_unit.decl {{.*}} fork hierarchy "vector_edge.{{[^"]*}}$covergroup_event.
// EVENT-LABEL: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT: simulation.ref.load {{%.*}} : !simulation.ref<!simulation.packed_array<3 : 0 x i1>> -> !simulation.packed_array<3 : 0 x i1>
// EVENT: simulation.packed.flatten {{%.*}} : (!simulation.packed_array<3 : 0 x i1>) -> i4
// EVENT: simulation.observer.bind @{{[^ ]+}} values({{.*}}) captures 1 : !simulation.observer<i4>
// EVENT: simulation.covergroup.clock_event.register
// EVENT-SAME: edges [1]
// EVENT-LABEL: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT: simulation.ref.load {{%.*}} : !simulation.ref<!simulation.packed_array<0 : 3 x !simulation.logic<1>>> -> !simulation.packed_array<0 : 3 x !simulation.logic<1>>
// EVENT: simulation.packed.flatten {{%.*}} : (!simulation.packed_array<0 : 3 x !simulation.logic<1>>) -> !simulation.logic<4>
// EVENT: simulation.observer.bind @{{[^ ]+}} values({{.*}}) captures 1 : !simulation.observer<!simulation.logic<4>>
// EVENT: simulation.covergroup.clock_event.register
// EVENT-SAME: edges [2]
// EVENT-LABEL: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}(
// EVENT: simulation.net.read {{%.*}} : !simulation.net<!simulation.packed_array<7 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
// EVENT: simulation.packed.flatten {{%.*}} : (!simulation.packed_array<7 : 0 x !simulation.logic<1>>) -> !simulation.logic<8>
// EVENT: simulation.observer.bind @{{[^ ]+}} values({{.*}}) captures 1 : !simulation.observer<!simulation.logic<8>>
// EVENT: simulation.covergroup.clock_event.register
// EVENT-SAME: edges [3]

// The observer bodies follow the sampler functions. The computed primary
// is the XOR of its two inputs, and the ref-formal evaluator reads its
// argument ref.
// EVENT: simulation.func private @[[PRIMARY_FN]](
// EVENT: arith.xori
// EVENT: simulation.func private @[[IFF_FN]](
// EVENT: simulation.func private @[[REF_EVALUATOR]](
// EVENT-SAME: !simulation.argument_ref<i1>
// EVENT: simulation.argument_ref.load

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

//--- others.sv
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

module computed;
  bit clock_a;
  bit clock_b;
  bit gate = 1;

  covergroup cg @(posedge (clock_a ^ clock_b) iff gate);
    point: coverpoint clock_a;
  endgroup

  cg coverage = new;
endmodule

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

module dynamic_sample_ref;
  bit clock;
  bit samples[$] = '{0, 1};
  int selected;

  covergroup cg(ref bit sampled) @(posedge clock iff sampled);
    point: coverpoint sampled;
  endgroup

cg coverage = new(samples[selected]);
endmodule

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
