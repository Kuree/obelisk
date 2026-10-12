// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -O3 --target=native -o %t.native %t/input.sv
// RUN: %t.native | FileCheck %s
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode -o %t.bytecode %t/input.sv
// RUN: %t.bytecode | FileCheck %s

// IEEE 8.13, 8.20 and 8.26: a class implements the shared interface through
// both diamond branches, using an inherited body or a concrete override.
// CHECK: inherited=7 interface=7
// CHECK: override=11 interface=11

//--- input.sv
interface class Value;
  pure virtual function int get();
endclass
interface class Left extends Value; endclass
interface class Right extends Value; endclass
interface class Diamond extends Left, Right; endclass
virtual class Base implements Diamond;
  virtual function int get(); return 7; endfunction
endclass
class Inherited extends Base; endclass
class Override extends Base;
  function int get(); return 11; endfunction
endclass
module top;
  Inherited inherited;
  Override overridden;
  Base base;
  Diamond view;
  initial begin
    inherited = new;
    overridden = new;
    base = inherited;
    view = inherited;
    $display("inherited=%0d interface=%0d", base.get(), view.get());
    base = overridden;
    view = overridden;
    $display("override=%0d interface=%0d", base.get(), view.get());
    $finish;
  end
endmodule
