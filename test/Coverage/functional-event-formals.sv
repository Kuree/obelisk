// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional %s -o %t
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT
// IEEE 6.17, 13.5, 15.5, 19.3 and 19.8.1: event inputs copy the shared
// synchronization identity; refs alias the event variable being reassigned.
module top;
  typedef event event_t;
  event_t first, second, actual;
  event untouched;

  function automatic bit replace_local(ref event selected, input event next);
    selected = next;
    return selected == next;
  endfunction

  covergroup capture(input event frozen, ref event live)
      with function sample(input event current);
    cp: coverpoint (frozen.triggered + 2 * live.triggered +
                    4 * current.triggered) {
      bins original_value = {5}; bins reassigned_value = {6};
    }
  endgroup
  covergroup local_copy with function sample(input event selected,
                                               input event next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref event selected,
                                                input event next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup nullable(input event frozen = null)
      with function sample(input event current = null);
    cp: coverpoint (frozen == null && current == null &&
                    !frozen.triggered && !current.triggered) { bins nil = {1}; }
  endgroup
  covergroup constructor_ref(ref event_t selected)
      with function sample(input event_t expected);
    cp: coverpoint (selected == expected) { bins yes = {1}; }
  endgroup

  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  constructor_ref writer;
  initial begin
    actual = first;
    captured = new(actual, actual);
    actual = second;
    -> first;
    captured.sample(first);
    #1;
    -> second;
    captured.sample(second);
    $display("capture=%0.0f", captured.get_inst_coverage());
    actual = first;
    copied = new();
    copied.sample(actual, second);
    $display("copy=%0.0f retained=%0d", copied.get_inst_coverage(),
             actual == first);
    live = new();
    live.sample(actual, second);
    $display("ref=%0.0f replaced=%0d", live.get_inst_coverage(),
             actual == second);
    empty_group = new();
    empty_group.sample();
    $display("null=%0.0f", empty_group.get_inst_coverage());
    // This variable has no direct assignment. Taking its address for a
    // read-only constructor ref still needs an event-variable storage cell.
    writer = new(untouched);
    writer.sample(untouched);
    $display("constructor_ref=%0.0f nonnull=%0d", writer.get_inst_coverage(),
             untouched != null);
    $finish;
  end
endmodule

// CHECK: capture=100
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: null=100
// CHECK: constructor_ref=100 nonnull=1

// Every formal is recorded as result_kind 12 with width 0 and no value type,
// including the sample and constructor refs. Exactly the defaulted formals
// carry the default flag and a default expression.

// JSON: "functional_formals":[
// JSON-DAG: "name":"selected","kind":2,"direction":1,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":1,"direction":2,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"expected","kind":2,"direction":1,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"live","kind":1,"direction":2,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":12,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":12,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":0}
// JSON-DAG: "name":"selected","kind":2,"direction":2,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":12,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON: "functional_container_types"
// INSPECT: functional-formals: 11
