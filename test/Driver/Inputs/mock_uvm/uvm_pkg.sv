// A minimal, self-contained UVM-shaped API for driver smoke testing. This is
// intentionally not a replacement for the Accellera UVM implementation.
package uvm_pkg;
  parameter int UVM_LOW = 100;

  class uvm_object;
    string name;

    function new(string name = "");
      this.name = name;
    endfunction

    virtual function string get_type_name();
      return "uvm_object";
    endfunction
  endclass

  class uvm_phase extends uvm_object;
    int unsigned objection_count;

    function new(string name = "run");
      super.new(name);
      objection_count = 0;
    endfunction

    task raise_objection(uvm_object source);
      objection_count++;
    endtask

    task drop_objection(uvm_object source);
      objection_count--;
    endtask
  endclass

  class uvm_component extends uvm_object;
    uvm_component parent;

    function new(string name, uvm_component parent = null);
      super.new(name);
      this.parent = parent;
    endfunction

    virtual task run_phase(uvm_phase phase);
    endtask
  endclass

  class uvm_test extends uvm_component;
    function new(string name, uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass
endpackage
