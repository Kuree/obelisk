// RUN: obelisk --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

// Source inventory must not bind dependent method signatures or bodies in
// this unused generic class, or duplicate an extern prototype's definition.
virtual class unused_methods #(type T);
  function new(); endfunction
  protected task run(T value);
    T local_value = value;
  endtask
  local static function T \E.p (T value);
    return value;
  endfunction
  extern function T external(T value);
  pure virtual function T deferred(T value);
endclass

function unused_methods::T unused_methods::external(T value);
  return value;
endfunction

module generic_source_method_import;
endmodule
