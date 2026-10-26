// RUN: obelisk --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

class base #(type T = int, int P = 3);
  function new(int n = P); endfunction
endclass
class unused #(type T) extends base#(T);
  function new(); super.new(T::value); endfunction
endclass
class header #(type T) extends base#(T)(T::value);
endclass
class external #(type T) extends base#(T);
  extern function new();
endclass
function external::new(); super.new(T::value); endfunction
class dependent #(type T) extends T::base_type;
endclass
class T; endclass
class shadow #(type T) extends T; endclass
module generic_source_inheritance_import;
endmodule
