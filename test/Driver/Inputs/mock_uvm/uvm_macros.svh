`define uvm_component_utils(TYPE) \
  virtual function string get_type_name(); \
    return "mock_component"; \
  endfunction

`define uvm_info(ID, MESSAGE, VERBOSITY) \
  $display("[%s] %s", ID, MESSAGE);
