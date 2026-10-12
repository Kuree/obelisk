// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional %s -o %t
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT
// LRM 9.7, 19.3 and 19.8.1: inputs preserve process identity, including after
// termination. Constructor refs track variables; sample refs may update them.
module top;
  process first, second, actual, untouched;
  function automatic bit replace_local(ref process selected, input process next);
    selected = next;
    return selected == next;
  endfunction
  covergroup capture(input process frozen, ref process live)
      with function sample(input process current);
    cp: coverpoint ((frozen.status() == process::FINISHED) +
                    2 * (frozen == live) + 4 * (current == frozen) +
                    8 * (live.status() == process::RUNNING)) {
      bins original_value = {7}; bins reassigned_value = {9};
    }
  endgroup
  covergroup local_copy with function sample(input process selected,
                                             input process next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref process selected,
                                              input process next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup nullable(input process frozen = null)
      with function sample(input process current);
    cp: coverpoint (frozen == null && current == null) { bins nil = {1}; }
  endgroup
  covergroup constructor_ref(ref process selected);
    cp: coverpoint (selected == null) { bins nil = {1}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  constructor_ref reader;
  initial begin
    fork begin first = process::self(); end join
    second = process::self();
    actual = first;
    captured = new(actual, actual);
    captured.sample(actual);
    actual = second;
    captured.sample(actual);
    $display("capture=%0.0f", captured.get_inst_coverage());
    actual = first;
    copied = new;
    copied.sample(actual, second);
    $display("copy=%0.0f retained=%0d", copied.get_inst_coverage(),
             actual == first);
    live = new;
    live.sample(actual, second);
    $display("ref=%0.0f replaced=%0d", live.get_inst_coverage(),
             actual == second);
    empty_group = new;
    empty_group.sample(null);
    $display("null=%0.0f", empty_group.get_inst_coverage());
    reader = new(untouched);
    reader.sample();
    $display("constructor_ref=%0.0f", reader.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: capture=100
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: null=100
// CHECK: constructor_ref=100

// Every formal is recorded as result_kind 14 with width 0 and no value type,
// including the sample and constructor refs. Exactly the defaulted formals
// carry the default flag and a default expression.

// JSON: "functional_formals":[
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":14,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":2,"direction":2,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":1,"direction":2,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"live","kind":1,"direction":2,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":2,"direction":1,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":14,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON: "functional_container_types"
// INSPECT: functional-formals: 10
