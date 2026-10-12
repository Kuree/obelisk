// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional %s -o %t
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 15.4, 19.3 and 19.8.1: copy handles, share messages and bounds,
// follow constructor refs, and preserve writable sample inputs and refs.
module top;
  typedef int alias_int;
  mailbox #(int) first, second, actual, untouched;
  mailbox #(alias_int) aliased;
  mailbox #(int unsigned) unsigned_box;
  mailbox #(string) text_box;
  mailbox dynamic_box;
  function automatic bit replace_local(ref mailbox #(int) selected,
                                        input mailbox #(int) next);
    selected = next;
    return selected == next;
  endfunction
  covergroup capture(input mailbox #(int) frozen, ref mailbox #(int) live)
      with function sample(input mailbox #(int) current);
    cp: coverpoint (frozen.num() + 2 * live.num() + 4 * current.num()) iff (current) {
      bins original_value = {7}; bins reassigned_value = {13}; bins shared_queue = {14};
    }
  endgroup
  covergroup local_copy with function sample(input mailbox #(int) selected,
                                             input mailbox #(int) next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref mailbox #(int) selected,
                                              input mailbox #(int) next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup nullable(input mailbox #(int) frozen = null)
      with function sample(input mailbox #(int) current);
    cp: coverpoint (frozen == null && current == null) iff (!current) { bins nil = {1}; }
  endgroup
  covergroup constructor_ref(ref mailbox #(int) selected);
    cp: coverpoint (selected == null) { bins nil = {1}; }
  endgroup
  covergroup nominal with function sample(input mailbox #(alias_int) a,
                                          input mailbox #(int unsigned) b,
                                          input mailbox #(string) c,
                                          input mailbox d);
    cp: coverpoint a.num() + b.num() + c.num() + d.num() { bins all_four = {4}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  constructor_ref reader;
  nominal types;
  initial begin
    first = new(2); second = new;
    first.put(1); second.put(2); second.put(3);
    $display("operators=%0d%0d%0d%0d%0d%0d%0d", first != second,
             first === first, first !== second, null == untouched,
             !untouched, (1'bx ? first : first) == first,
             (1'bx ? first : second) == null);
    actual = first;
    captured = new(actual, actual);
    captured.sample(actual);
    actual = second;
    captured.sample(actual);
    first.put(4);
    captured.sample(actual);
    $display("capture=%0.0f full=%0d", captured.get_inst_coverage(), first.try_put(5));
    actual = first;
    copied = new; copied.sample(actual, second);
    $display("copy=%0.0f retained=%0d", copied.get_inst_coverage(), actual == first);
    live = new; live.sample(actual, second);
    $display("ref=%0.0f replaced=%0d", live.get_inst_coverage(), actual == second);
    empty_group = new; empty_group.sample(null);
    $display("null=%0.0f", empty_group.get_inst_coverage());
    reader = new(untouched); reader.sample();
    $display("constructor_ref=%0.0f", reader.get_inst_coverage());
    aliased = first;
    unsigned_box = new; text_box = new; dynamic_box = new;
    unsigned_box.put(7); text_box.put("hello"); dynamic_box.put(1);
    // Use a separate empty alias to sample one message from each mailbox.
    aliased = new; aliased.put(6);
    types = new; types.sample(aliased, unsigned_box, text_box, dynamic_box);
    $display("nominal=%0.0f", types.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: operators=1111111
// CHECK: capture=100 full=0
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: null=100
// CHECK: constructor_ref=100
// CHECK: nominal=100

// Every mailbox formal is result_kind 17 with width 0. Its value_type names a
// kind-4 container descriptor keyed by the mailbox specialization: int and its
// typedef alias share one descriptor, while unsigned, string and the untyped
// mailbox each get their own. Only the nullable constructor input carries the
// default flag and a default expression.
// JSON: "functional_formals":[
// JSON-DAG: "name":"a","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT:[0-9]+]]}
// JSON-DAG: "name":"b","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[UNS:[0-9]+]]}
// JSON-DAG: "name":"c","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":2,"default_expression":0,"value_type":[[STR:[0-9]+]]}
// JSON-DAG: "name":"d","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":3,"default_expression":0,"value_type":[[DYN:[0-9]+]]}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"live","kind":1,"direction":2,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"selected","kind":1,"direction":2,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"selected","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"selected","kind":2,"direction":2,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"next","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[INT]]}
// JSON-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":17,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":[[INT]]}
// JSON-DAG: "name":"current","kind":2,"direction":1,"result_kind":17,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[INT]]}
// JSON: "functional_container_types":[
// JSON-DAG: {"id":[[INT]],"kind":4,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// JSON-DAG: {"id":[[UNS]],"kind":4,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// JSON-DAG: {"id":[[STR]],"kind":4,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// JSON-DAG: {"id":[[DYN]],"kind":4,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// INSPECT: functional-formals: 14
