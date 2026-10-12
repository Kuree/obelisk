// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 19.3: an input retains its constructor value, while a ref retains its
// storage binding, including class and automatic storage. Only two small
// configurations are needed; no scaling sweep belongs in this regression.
module top;
  string observed = "a";
  real observed_real = 1.0;
  int calls, defaults_called, covered, total;
  real percentage;
  function automatic string input_once();
    calls++;
    return observed;
  endfunction
  function automatic string default_once();
    defaults_called++;
    return "dd";
  endfunction
  covergroup cg(input string frozen, ref string live, ref real live_real);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    frozen_point: coverpoint frozen.len() {
      bins one = {1}; bins two = {2};
    }
    live_point: coverpoint live.len() iff (live != "skip") {
      bins one = {1}; bins three = {3};
    }
    real_point: coverpoint live_real {
      bins one = {1.0}; bins two = {2.0};
    }
  endgroup
  covergroup defaults(input string label = default_once());
    option.per_instance = 1;
    cp: coverpoint label.len() { bins two = {2}; bins three = {3}; }
  endgroup
  class storage_box;
    string text = "a";
  endclass
  cg a, b, class_ref, automatic_ref;
  defaults defaulted;
  storage_box object;
  function automatic cg make_auto();
    string local_text = "a";
    cg made = new("cc", local_text, observed_real);
    local_text = "ccc";
    return made;
  endfunction
  initial begin
    a = new(input_once(), observed, observed_real);
    b = new("cc", observed, observed_real);
    a.set_inst_name("snapshot-one");
    b.set_inst_name("snapshot-two");
    a.sample();
    b.sample();
    observed = "ccc";
    observed_real = 2.0;
    a.sample();
    b.sample();
    observed = "skip";
    a.sample();
    percentage = a.frozen_point.get_inst_coverage(covered, total);
    $display("frozen %.0f %0d %0d", percentage, covered, total);
    percentage = a.live_point.get_inst_coverage(covered, total);
    $display("live %.0f %0d %0d", percentage, covered, total);
    percentage = a.real_point.get_inst_coverage(covered, total);
    $display("real %.0f %0d %0d", percentage, covered, total);
    percentage = b.frozen_point.get_inst_coverage(covered, total);
    $display("independent %.0f %0d %0d", percentage, covered, total);

    object = new;
    class_ref = new("cc", object.text, observed_real);
    class_ref.set_inst_name("class-ref");
    class_ref.sample();
    object.text = "ccc";
    object = null;
    class_ref.sample();
    percentage = class_ref.live_point.get_inst_coverage(covered, total);
    $display("class %.0f %0d %0d", percentage, covered, total);
    automatic_ref = make_auto();
    automatic_ref.set_inst_name("automatic-ref");
    automatic_ref.sample();
    percentage = automatic_ref.live_point.get_inst_coverage(covered, total);
    $display("automatic %.0f %0d %0d", percentage, covered, total);
    defaulted = new;
    defaulted.set_inst_name("defaulted");
    defaulted.sample();
    defaulted.sample();
    percentage = defaulted.cp.get_inst_coverage(covered, total);
    $display("default %.0f %0d %0d calls=%0d defaults=%0d", percentage,
             covered, total, calls, defaults_called);
    $finish;
  end
endmodule

// CHECK: frozen 50 1 2
// CHECK-NEXT: live 100 2 2
// CHECK-NEXT: real 100 2 2
// CHECK-NEXT: independent 50 1 2
// CHECK-NEXT: class 100 2 2
// CHECK-NEXT: automatic 50 1 2
// CHECK-NEXT: default 50 1 2 calls=1 defaults=1

// Each instance counts the strings it sampled: values frozen at construction,
// the live string a ref formal names, and the converted real formal. Items
// within an instance follow resolved identities.
// REPORT: "functional":{"available":true,"covered":18,"total":26,
// REPORT: {"type":{{[0-9]+}},"name":"snapshot-one",
// REPORT-DAG: "name":"frozen_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":3,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":0,
// REPORT-DAG: "name":"live_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":1,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"three",{{[^[]*}}"count":1,
// REPORT-DAG: "name":"real_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":1,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":2,
// REPORT: {"type":{{[0-9]+}},"name":"snapshot-two",
// REPORT-DAG: "name":"frozen_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":0,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":2,
// REPORT-DAG: "name":"live_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":1,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"three",{{[^[]*}}"count":1,
// REPORT-DAG: "name":"real_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":1,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":1,
// REPORT: {"type":{{[0-9]+}},"name":"class-ref",
// REPORT-DAG: "name":"frozen_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":0,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":2,
// REPORT-DAG: "name":"live_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":1,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"three",{{[^[]*}}"count":1,
// REPORT-DAG: "name":"real_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":0,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":2,
// REPORT: {"type":{{[0-9]+}},"name":"automatic-ref",
// REPORT-DAG: "name":"frozen_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":0,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":1,
// REPORT-DAG: "name":"live_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":0,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"three",{{[^[]*}}"count":1,
// REPORT-DAG: "name":"real_point",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"one",{{[^[]*}}"count":0,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"two",{{[^[]*}}"count":1,
// REPORT: {"type":{{[0-9]+}},"name":"defaulted",
// REPORT-DAG: "name":"cp",{{[^[]*}}"targets":[],"bins":[{{[^]]*}}"name":"two",{{[^[]*}}"count":2,{{[^[]*}}[{{[^]]*}}]},{{[{][^[]*}}"name":"three",{{[^[]*}}"count":0,
// REPORT: "functional_counters":
// INSPECT: functional-configurations: 2
