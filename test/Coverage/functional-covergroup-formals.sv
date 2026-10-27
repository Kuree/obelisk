// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"base":{{[1-9]}}'
// RUN: talos-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 19.3 and 19.8.1: inputs copy the handle to a shared coverage instance;
// constructor refs track the variable, and sample refs may update it.
module top;
  int value;
  covergroup source_cg;
    option.per_instance = 1;
    cp: coverpoint value { bins values[] = {1, 2}; }
  endgroup
  source_cg first, second, actual, untouched;

  function automatic bit replace_local(ref source_cg selected,
                                       input source_cg next);
    selected = next;
    return selected == next;
  endfunction
  covergroup capture(input source_cg frozen, ref source_cg live)
      with function sample(input source_cg current);
    cp: coverpoint int'(frozen.get_inst_coverage() + live.get_inst_coverage() +
                       current.get_inst_coverage()) {
      bins original_value = {150};
      bins reassigned_value = {250};
      bins shared_instance = {300};
    }
  endgroup
  covergroup local_copy with function sample(input source_cg selected,
                                             input source_cg next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref source_cg selected,
                                              input source_cg next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup nullable(input source_cg frozen = null)
      with function sample(input source_cg current);
    cp: coverpoint (frozen == null && current == null) { bins nil = {1}; }
  endgroup
  covergroup constructor_ref(ref source_cg selected);
    cp: coverpoint (selected == null) { bins nil = {1}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  constructor_ref reader;
  initial begin
    first = new;
    value = 1;
    first.sample();
    actual = first;
    captured = new(actual, actual);
    captured.sample(actual);
    second = new;
    second.sample();
    value = 2;
    second.sample();
    actual = second;
    captured.sample(actual);
    first.sample();
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

// Runtime: inputs copy the handle and share the coverage instance; constructor
// refs track the variable and sample refs update it.
// CHECK: capture=100
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: null=100
// CHECK: constructor_ref=100

// The batch records ten covergroup-handle formals (result_kind 13, width 0),
// every one naming the same nominal source_cg type, and exactly one of them
// (nullable.frozen) has a default expression. No covergroup type has a base.
// INSPECT: functional-formals: 10
// JSON-DAG: {{[{]}}"id":[[LOCAL_COPY:[0-9]+]],{{[^}]*}}"name":"local_copy"
// JSON-DAG: {{[{]}}"id":[[CTOR_REF:[0-9]+]],{{[^}]*}}"name":"constructor_ref"
// JSON-DAG: {{[{]}}"id":[[CAPTURE:[0-9]+]],{{[^}]*}}"name":"capture"
// JSON-DAG: {{[{]}}"id":[[NULLABLE:[0-9]+]],{{[^}]*}}"name":"nullable"
// JSON-DAG: {{[{]}}"id":[[LIVE:[0-9]+]],{{[^}]*}}"name":"live_sample"
// JSON-DAG: {{[{]}}"id":[[SRC:[0-9]+]],{{[^}]*}}"name":"source_cg"
// JSON: "functional_formals":[
// JSON-DAG: "type":[[LOCAL_COPY]],"name":"selected","kind":2,"direction":1,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[LOCAL_COPY]],"name":"next","kind":2,"direction":1,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[CTOR_REF]],"name":"selected","kind":1,"direction":2,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[CAPTURE]],"name":"frozen","kind":1,"direction":1,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[CAPTURE]],"name":"live","kind":1,"direction":2,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[CAPTURE]],"name":"current","kind":2,"direction":1,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[NULLABLE]],"name":"frozen","kind":1,"direction":1,"result_kind":13,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":[[SRC]]}
// JSON-DAG: "type":[[NULLABLE]],"name":"current","kind":2,"direction":1,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[LIVE]],"name":"selected","kind":2,"direction":2,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[SRC]]}
// JSON-DAG: "type":[[LIVE]],"name":"next","kind":2,"direction":1,"result_kind":13,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[SRC]]}
