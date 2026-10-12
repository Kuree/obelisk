// RUN: %target_clang -fPIC -c %S/Inputs/chandle-formals.c -o %t.o
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional %s %t.o -o %t
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 6.14, 13.5, 19.3 and 19.8.1: chandle inputs copy opaque C pointer
// identities, refs alias the variable, and default handles are null.
module top;
  import "DPI-C" pure function chandle coverage_chandle(input int index);
  chandle first, second, actual;
  int step;

  function automatic bit clear_local(ref chandle value);
    bit present;
    present = value != null;
    value = null;
    return present;
  endfunction

  covergroup capture(input chandle frozen, ref chandle live)
      with function sample(input chandle current);
    cp: coverpoint ((frozen == first && live == current) ? step : -1) {
      bins original_value = {1}; bins reassigned_value = {2};
    }
  endgroup
  covergroup local_copy with function sample(input chandle current);
    cp: coverpoint clear_local(current) { bins nonnull = {1}; }
  endgroup
  covergroup live_sample with function sample(ref chandle current);
    cp: coverpoint clear_local(current) { bins nonnull = {1}; }
  endgroup
  covergroup nullable(input chandle frozen = null)
      with function sample(input chandle current = null);
    cp: coverpoint (frozen == null && current == null) { bins nil = {1}; }
  endgroup

  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  initial begin
    first = coverage_chandle(0);
    second = coverage_chandle(1);
    actual = first;
    captured = new(actual, actual);
    step = 1;
    captured.sample(first);
    actual = second;
    step = 2;
    captured.sample(second);
    $display("capture=%0.0f distinct=%0d", captured.get_inst_coverage(),
             first != second);
    copied = new();
    copied.sample(actual);
    $display("copy=%0.0f retained=%0d", copied.get_inst_coverage(),
             actual == second);
    live = new();
    live.sample(actual);
    $display("ref=%0.0f cleared=%0d", live.get_inst_coverage(), actual == null);
    empty_group = new();
    empty_group.sample();
    $display("null=%0.0f", empty_group.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: capture=100 distinct=1
// CHECK: copy=100 retained=1
// CHECK: ref=100 cleared=1
// CHECK: null=100

// Every chandle formal is recorded as result_kind 11 with width 0 and no
// value type; the two formals with defaults carry the default flag and a
// default expression, the others neither.
// JSON: "functional_formals":[
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":11,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"live","kind":1,"direction":2,"result_kind":11,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":11,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":2,"result_kind":11,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":11,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":11,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":11,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":0}
// JSON: "functional_container_types"
// INSPECT: functional-formals: 7
