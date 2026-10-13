// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

module native_concurrent_sva_clock_formal_flow;
  logic source_clock = 0, destination_clock = 0;
  logic first = 0, second = 0;
  logic nested_hit = 0, sequence_hit = 0, handoff_hit = 0;

  sequence sampled(event sampling, logic value);
    @sampling value;
  endsequence

  property nested_property(event sampling, logic value);
    sampled(sampling, value);
  endproperty

  sequence pass_sequence(sequence actual);
    actual;
  endsequence

  property pass_property(sequence actual);
    actual;
  endproperty

  sequence handoff(event source_event, event destination_event,
                   logic source_value, logic destination_value);
    @source_event source_value ##1 @destination_event destination_value;
  endsequence

  cover property (nested_property(posedge source_clock, first))
    nested_hit = 1;
  cover property (pass_property(
      pass_sequence(sampled(posedge destination_clock, second))))
    sequence_hit = 1;
  cover property (pass_property(handoff(posedge source_clock,
                                        posedge destination_clock,
                                        first, second)))
    handoff_hit = 1;

  initial begin
    #1 first = 1; second = 1;
    #1 source_clock = 1;
    #1 source_clock = 0;
    #1 destination_clock = 1;
    #1 destination_clock = 0;
    #1 $display("clock-formal nested=%0d sequence=%0d handoff=%0d",
                nested_hit, sequence_hit, handoff_hit);
    $finish;
  end
endmodule

// CHECK: clock-formal nested=1 sequence=1 handoff=1
