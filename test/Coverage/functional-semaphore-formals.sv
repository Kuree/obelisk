// RUN: %talos --std=1800-2023 -O0 --coverage=functional %s -o %t
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: talos-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 15.3, 19.3 and 19.8.1: inputs share buckets, constructor refs follow
// variables, and sample inputs and refs retain their distinct assignment rules.
module top;
  semaphore first, second, actual, untouched;
  function automatic bit replace_local(ref semaphore selected, input semaphore next);
    selected = next;
    return selected == next;
  endfunction
  function automatic bit available(input semaphore selected, input int count);
    bit result_value = selected.try_get(count);
    if (result_value) selected.put(count);
    return result_value;
  endfunction
  covergroup capture(input semaphore frozen, ref semaphore live)
      with function sample(input semaphore current);
    cp: coverpoint (available(frozen, 2) + 2 * (frozen == live) +
                    4 * (current == frozen) + 8 * available(live, 3)) iff (current) {
      bins original_value = {7}; bins reassigned_value = {9};
      bins shared_bucket = {8};
    }
  endgroup
  covergroup local_copy with function sample(input semaphore selected,
                                             input semaphore next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref semaphore selected,
                                              input semaphore next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup nullable(input semaphore frozen = null)
      with function sample(input semaphore current);
    cp: coverpoint (frozen == null && current == null) iff (!current) { bins nil = {1}; }
  endgroup
  covergroup constructor_ref(ref semaphore selected);
    cp: coverpoint (selected == null) { bins nil = {1}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  constructor_ref reader;
  initial begin
    first = new(2);
    second = new(3);
    $display("operators=%0d%0d%0d%0d%0d%0d%0d", first != second,
             first === first, first !== second, null == untouched,
             !untouched, (1'bx ? first : first) == first,
             (1'bx ? first : second) == null);
    actual = first;
    captured = new(actual, actual);
    captured.sample(actual);
    actual = second;
    captured.sample(actual);
    void'(first.try_get(2));
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

// CHECK: operators=1111111
// CHECK: capture=100
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: null=100
// CHECK: constructor_ref=100

// Every semaphore formal is result_kind 16 with width 0 and no value type;
// only the nullable constructor input carries the default flag and a default
// expression. The two local_copy/live_sample formal sets differ by direction.
// JSON: "functional_formals":[
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"live","kind":1,"direction":2,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":1,"direction":2,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":2,"direction":1,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"selected","kind":2,"direction":2,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":0}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":16,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":0}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":16,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":0}
// JSON: "functional_container_types"
// INSPECT: functional-formals: 10
