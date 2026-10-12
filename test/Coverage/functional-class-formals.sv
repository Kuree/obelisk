// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"class_type":' --implicit-check-not='"value_type":'
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 8.12, 8.14, 13.5, 19.3 and 19.8.1: inputs copy class handles,
// retaining shared object identity. Refs alias the handle variable itself.
interface class Readable;
  pure virtual function int read_value();
endclass

class Record implements Readable;
  int number;
  function new(int value); number = value; endfunction
  virtual function int read_value(); return number; endfunction
endclass

class Derived extends Record;
  function new(int value); super.new(value); endfunction
endclass

// A null-only formal still needs its nominal class descriptor.
class Unallocated; endclass

module top;
  Record actual, replacement, retained;
  Derived derived;
  function automatic int replace_local(ref Record local_value, input Record next_value);
    local_value = next_value;
    return local_value.number;
  endfunction

  covergroup capture(input Record frozen, ref Record live)
      with function sample(input Record local_value);
    cp: coverpoint frozen.number + live.number + local_value.number {
      bins first_value = {11}; bins second_value = {13};
    }
  endgroup
  covergroup local_copy with function sample(input Record local_value,
                                               input Record next_value);
    cp: coverpoint replace_local(local_value, next_value) { bins nine = {9}; }
  endgroup
  covergroup live_sample with function sample(ref Record local_value,
                                               input Record next_value);
    cp: coverpoint replace_local(local_value, next_value) { bins nine = {9}; }
  endgroup
  covergroup interface_capture(input Readable view);
    cp: coverpoint view.read_value() { bins first_value = {5}; bins next_value = {9}; }
  endgroup
  covergroup nullable(input Record missing = null)
      with function sample(input Record local_value);
    cp: coverpoint (missing == null && local_value == null) { bins nil = {1}; }
  endgroup
  covergroup null_only(input Unallocated missing = null);
    cp: coverpoint (missing == null) { bins nil = {1}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  interface_capture polymorphic;
  nullable empty_group;
  null_only unallocated_group;

  initial begin
    derived = new(1);
    actual = derived;
    captured = new(derived, actual);
    actual.number = 3;
    derived = new(4);
    actual = derived;
    captured.sample(actual);
    actual.number = 5;
    captured.sample(actual);
    $display("capture=%0.0f", captured.get_inst_coverage());
    retained = actual;
    polymorphic = new(actual);
    polymorphic.sample();
    replacement = new(9);
    copied = new();
    copied.sample(actual, replacement);
    $display("local=%0d coverage=%0.0f", actual.number, copied.get_inst_coverage());
    live = new();
    live.sample(actual, replacement);
    $display("live=%0d coverage=%0.0f", actual.number, live.get_inst_coverage());
    retained.number = 9;
    polymorphic.sample();
    $display("interface=%0.0f", polymorphic.get_inst_coverage());
    empty_group = new();
    empty_group.sample(null);
    $display("null=%0.0f", empty_group.get_inst_coverage());
    unallocated_group = new();
    unallocated_group.sample();
    $display("unallocated=%0.0f", unallocated_group.get_inst_coverage());
    $finish;
  end
endmodule

// Runtime: inputs copy the handle and keep object identity; refs alias the
// handle variable.
// CHECK: capture=100
// CHECK: local=5 coverage=100
// CHECK: live=9 coverage=100
// CHECK: interface=100
// CHECK: null=100
// CHECK: unallocated=100

// The batch records eleven formals (INSPECT, and the implicit-check-not on
// "value_type" covers exactly eleven), all handle-valued (result_kind 10,
// width 0), each naming the nominal class descriptor of its declared type:
// Record, Readable or the null-only Unallocated class. The three class types
// have distinct layouts, so equal 64-bit representations never conflate them.
// INSPECT: functional-formals: 11
// JSON-DAG: {{[{]}}"id":[[NULLABLE:[0-9]+]],{{[^}]*}}"name":"nullable"
// JSON-DAG: {{[{]}}"id":[[LOCAL_COPY:[0-9]+]],{{[^}]*}}"name":"local_copy"
// JSON-DAG: {{[{]}}"id":[[IFACE:[0-9]+]],{{[^}]*}}"name":"interface_capture"
// JSON-DAG: {{[{]}}"id":[[NULL_ONLY:[0-9]+]],{{[^}]*}}"name":"null_only"
// JSON-DAG: {{[{]}}"id":[[CAPTURE:[0-9]+]],{{[^}]*}}"name":"capture"
// JSON-DAG: {{[{]}}"id":[[LIVE:[0-9]+]],{{[^}]*}}"name":"live_sample"
// JSON: "functional_formals":[
// JSON-DAG: "type":[[NULLABLE]],"name":"missing","kind":1,"direction":1,"result_kind":10,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":[[REC:[0-9]+]]}
// JSON-DAG: "type":[[NULLABLE]],"name":"local_value","kind":2,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[LOCAL_COPY]],"name":"local_value","kind":2,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[LOCAL_COPY]],"name":"next_value","kind":2,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[IFACE]],"name":"view","kind":1,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[READABLE:[0-9]+]]}
// JSON-DAG: "type":[[NULL_ONLY]],"name":"missing","kind":1,"direction":1,"result_kind":10,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":[[UNALLOC:[0-9]+]]}
// JSON-DAG: "type":[[CAPTURE]],"name":"frozen","kind":1,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[CAPTURE]],"name":"live","kind":1,"direction":2,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[CAPTURE]],"name":"local_value","kind":2,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[LIVE]],"name":"local_value","kind":2,"direction":2,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[REC]]}
// JSON-DAG: "type":[[LIVE]],"name":"next_value","kind":2,"direction":1,"result_kind":10,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[REC]]}
// JSON: "functional_class_types":[
// JSON-DAG: {"id":[[REC]],"class_type":3}
// JSON-DAG: {"id":[[UNALLOC]],"class_type":4}
// JSON-DAG: {"id":[[READABLE]],"class_type":2}
