// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: talos-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 25.9, 19.3 and 19.8.1: copied inputs preserve interface identity, refs
// follow variables, and specialization and modport remain part of the type.
interface bus #(int WIDTH = 8);
  int value;
  modport view(input value);
endinterface
module top;
  bus #(8) first(), second();
  bus #(4) narrow_bus();
  bus #(12) unused_bus();
  virtual bus #(8) actual, untouched;
  function automatic bit replace_local(ref virtual bus #(8) selected,
                                        input virtual bus #(8) next);
    selected = next;
    return selected == next;
  endfunction
  covergroup capture(input virtual bus #(8) frozen, ref virtual bus #(8) live)
      with function sample(input virtual bus #(8).view current);
    cp: coverpoint frozen.value + live.value + current.value {
      bins first_value = {5}; bins second_value = {7}; bins underlying_update = {10};
    }
  endgroup
  covergroup local_copy with function sample(input virtual bus #(8) selected,
                                             input virtual bus #(8) next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref virtual bus #(8) selected,
                                              input virtual bus #(8) next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup nullable(input virtual bus #(8) frozen = null)
      with function sample(input virtual bus #(8) current = null);
    cp: coverpoint (frozen == null && current == null) { bins nil = {1}; }
  endgroup
  covergroup constructor_ref(ref virtual bus #(8) selected);
    cp: coverpoint (selected == null) { bins nil = {1}; }
  endgroup
  covergroup narrow with function sample(input virtual bus #(4) selected);
    cp: coverpoint selected.value { bins four = {4}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  nullable empty_group;
  constructor_ref reader;
  narrow small_group;
  initial begin
    first.value = 1;
    second.value = 2;
    narrow_bus.value = 4;
    actual = first;
    captured = new(actual, actual);
    actual = second;
    captured.sample(actual);
    second.value = 3;
    captured.sample(actual);
    first.value = 4;
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
    empty_group.sample();
    $display("null=%0.0f", empty_group.get_inst_coverage());
    reader = new(untouched);
    reader.sample();
    $display("constructor_ref=%0.0f", reader.get_inst_coverage());
    small_group = new;
    small_group.sample(narrow_bus);
    $display("specialization=%0.0f", small_group.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: capture=100
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: null=100
// CHECK: constructor_ref=100
// CHECK: specialization=100

// All eleven virtual-interface formals are result_kind 15 with width 0 and a
// value_type naming a shared type identity. Three identities exist: bus #(8),
// its "view" modport (own identity, same interface_type, used only by the
// capture sample formal) and bus #(4); only the two nullable formals carry the
// default flag and expression.
// JSON: "functional_formals":[
// JSON-DAG: {"id":{{[0-9]+}},"type":[[CAP:[0-9]+]],"name":"live","kind":1,"direction":2,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[BUS:[0-9]+]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[CAP]],"name":"frozen","kind":1,"direction":1,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[CAP]],"name":"current","kind":2,"direction":1,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[VIEW:[0-9]+]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[COPY:[0-9]+]],"name":"selected","kind":2,"direction":1,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[COPY]],"name":"next","kind":2,"direction":1,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[LIVE:[0-9]+]],"name":"selected","kind":2,"direction":2,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[LIVE]],"name":"next","kind":2,"direction":1,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":1,"default_expression":0,"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[NULLABLE:[0-9]+]],"name":"frozen","kind":1,"direction":1,"result_kind":15,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":[[NULLABLE]],"name":"current","kind":2,"direction":1,"result_kind":15,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}},"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":{{[0-9]+}},"name":"selected","kind":1,"direction":2,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[BUS]]}
// JSON-DAG: {"id":{{[0-9]+}},"type":{{[0-9]+}},"name":"selected","kind":2,"direction":1,"result_kind":15,"flags":0,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":0,"value_type":[[NARROW:[0-9]+]]}
// JSON: "functional_virtual_interface_types":[
// JSON-SAME: {"id":[[NARROW]],"interface_type":[[NARROW]]},
// JSON-SAME: {"id":[[VIEW]],"interface_type":[[BUS]]},
// JSON-SAME: {"id":[[BUS]],"interface_type":[[BUS]]}],
// JSON-SAME: "functional_virtual_interface_scopes":[
// JSON-SAME: {"id":2,"interface_type":[[BUS]]},
// JSON-SAME: {"id":3,"interface_type":[[BUS]]},
// JSON-SAME: {"id":4,"interface_type":[[NARROW]]}],
// INSPECT: functional-formals: 11
