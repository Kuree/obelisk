// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 19.4.1: a base-class view still names the derived covergroup instance.
// The formal retains its static base type and accepts the dynamic derived type.
class base;
  int data;
  covergroup cg;
    cp: coverpoint data;
  endgroup
  function new(); cg = new; endfunction
endclass
class derived extends base;
  covergroup extends cg;
    other: coverpoint data;
  endgroup
endclass
module top;
  derived object_value;
  base base_object;
  typedef type(base_object.cg) group_t;
  group_t value;
  covergroup observer(input group_t held);
    cp: coverpoint (held != null) { bins yes = {1}; }
  endgroup
  observer value_observer;
  initial begin
    object_value = new;
    base_object = object_value;
    value = base_object.cg;
    value_observer = new(value);
    value_observer.sample();
    $display("coverage=%0.0f", value_observer.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: coverage=100

// The nominal hierarchy has exactly one derived type (the extended covergroup)
// whose base is the original covergroup; the observer and the original have no
// base. The single formal is handle-valued (result_kind 13) and names the
// static base type, not the dynamic derived one.
// INSPECT: functional-types: 3
// INSPECT: functional-formals: 1
// JSON-DAG: {{[{]}}"id":[[DERIVED:[0-9]+]],{{[^}]*}}"base":[[BASE:[1-9][0-9]*]],{{[^}]*}}"hierarchy":"derived::cg"
// JSON-DAG: {{[{]}}"id":[[OBSERVER:[0-9]+]],{{[^}]*}}"name":"observer","base":0,
// JSON-DAG: {{[{]}}"id":[[BASE]],{{[^}]*}}"base":0,{{[^}]*}}"hierarchy":"base::cg"
// JSON: "functional_formals":[
// JSON-SAME: "type":[[OBSERVER]],"name":"held","kind":1,"direction":1,"result_kind":13,{{[^}]*}}"value_type":[[BASE]]}
