// RUN: obelisk --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

class item;
  static int count = 10;
  int value = 1;

  function new(int value = 2);
    this.value = value;
    count++;
  endfunction

  static function int current();
    return count;
  endfunction

  virtual function int get(input int bias = 0);
    return value + bias;
  endfunction

  virtual function item identity(input item other);
    return other;
  endfunction
endclass

class child extends item;
  int extra = 4;

  function new(int value = 3);
    super.new(value);
  endfunction

  virtual function int get(input int bias = 0);
    return super.get(bias) + extra;
  endfunction

  virtual function item identity(input item other);
    return other;
  endfunction
endclass

virtual class abstract_item;
  pure virtual function int get_value();
endclass

class concrete_item extends abstract_item;
  int value = 8;

  virtual function int get_value();
    return value;
  endfunction
endclass

interface class readable;
  pure virtual function int read();
endclass

class readable_item implements readable;
  int value = 12;

  virtual function int read();
    return value;
  endfunction
endclass

class default_base;
  int value;

  function new(int seed = 13);
    value = seed;
  endfunction
endclass

class default_child extends default_base();
endclass

module top;
  initial begin
    item a;
    item b;
    child c;
    child casted;
    item identity_result;
    abstract_item abstract_handle;
    concrete_item concrete;
    readable_item readable_object;
    default_child default_object;
    int cast_ok;

    c = new(5);
    a = c;
    b = new a;
    cast_ok = $cast(casted, a);
    identity_result = a.identity(b);
    a = null;
    concrete = new;
    abstract_handle = concrete;
    readable_object = new;
    default_object = new;
    $display("%0d %0d %0d %0d %0d %0d %0d %0d",
             b.get(1), item::current(), cast_ok, casted.get(),
             abstract_handle.get_value(), readable_object.read(),
             default_object.value, identity_result.get(1));
  end
endmodule

// CHECK: 10 11 1 9 8 12 13 10
