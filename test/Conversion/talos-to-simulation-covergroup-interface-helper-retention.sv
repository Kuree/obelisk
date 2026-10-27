// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --talos-sim-prepare | FileCheck %s --check-prefix=RETAIN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RESULT

// IEEE 1800-2023 8.26 and 19.5: a coverpoint calling through an interface
// class dispatches to the implementing class method, inherited or overridden.
// The sample helper is outside the design body, so preparation must retain the
// interface prototype and both implementations (Base, inherited by Inherited,
// and Override) as roots and keep their class method entries bound to units.
// RETAIN: coverage.retained_symbols = [{{.*}}@[[IFACE:__talos_class_[A-Za-z0-9_]*Readable_method_0]]{{.*}}@[[BASE:__talos_class_[A-Za-z0-9_]*Base_method_0]]{{.*}}@[[OVERRIDE:__talos_class_[A-Za-z0-9_]*Override_method_0]]
// RETAIN-DAG: simulation.class.method @[[IFACE]] of @{{.*}}Readable slot {{[0-9]+}} signature_id {{[0-9]+}} interface_ordinal 0
// RETAIN-DAG: simulation.class.method @[[BASE]] of @{{.*}}Base slot 0 signature_id {{[0-9]+}} implemented_by @unit_{{[0-9]+}}
// RETAIN-DAG: simulation.class.method @[[OVERRIDE]] of @{{.*}}Override slot 0 signature_id {{[0-9]+}} implemented_by @unit_{{[0-9]+}}

// The retained implementations produce the sampled values 2 and 3.
// RESULT: interface coverage=100

interface class Readable;
  pure virtual function int read_value();
endclass
class Base implements Readable;
  virtual function int read_value(); return 2; endfunction
endclass
class Inherited extends Base; endclass
class Override extends Base;
  function int read_value(); return 3; endfunction
endclass
module top;
  Readable view;
  Inherited inherited;
  Override overridden;
  covergroup cg;
    cp: coverpoint view.read_value() { bins values[] = {2, 3}; }
  endgroup
  cg group_value;
  initial begin
    inherited = new;
    overridden = new;
    group_value = new;
    view = inherited;
    group_value.sample();
    view = overridden;
    group_value.sample();
    $display("interface coverage=%0.0f", group_value.get_inst_coverage());
    $finish;
  end
endmodule
