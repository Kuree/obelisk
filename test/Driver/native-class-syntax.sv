// RUN: obelisk -fno-lto --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

interface class readable;
  pure virtual function int read();
endclass

interface class named extends readable;
  pure virtual function int tag();
endclass

virtual class abstract_item;
  pure virtual function int get_value();
endclass

class base #(type value_t = int, int initial_value = 2) implements named;
  protected value_t value;
  local int secret = 3;
  static int count = 10;

  function new(value_t value = initial_value);
    this.value = value;
    count++;
  endfunction

  virtual function int read();
    return value;
  endfunction

  virtual function int tag();
    return secret;
  endfunction

  virtual function int get(input int bias = 0);
    return value + bias;
  endfunction

  static function int current();
    return count;
  endfunction
endclass

class child extends base #(int, 5);
  function new(int value = 6);
    super.new(value);
  endfunction

  virtual function int get(input int bias = 0);
    return super.get(bias) + tag();
  endfunction
endclass

class concrete_item extends abstract_item;
  virtual function int get_value();
    return 8;
  endfunction
endclass

class external_item;
  extern function new(int value = 14);
  extern virtual function int get();
  int value;
endclass

function external_item::new(int value = 14);
  this.value = value;
endfunction

function int external_item::get();
  return value;
endfunction

module top;
  initial begin
    automatic child object = new();
    automatic child retained = object;
    automatic base #(int, 5) base_handle = object;
    automatic readable readable_handle = object;
    automatic named named_handle;
    automatic base #(int, 5) plain = new(1);
    automatic concrete_item concrete = new;
    automatic abstract_item abstract_handle = concrete;
    automatic external_item external_object = new;
    automatic int cast_interface = $cast(named_handle, readable_handle);
    automatic int cast_failure = $cast(retained, plain);
    automatic int retained_value = retained.get();
    automatic int cast_null = $cast(retained, null);
    $display("%0d %0d %0d %0d %0d %0d %0d %0d %0d",
             base_handle.get(1), named_handle.read(), named_handle.tag(),
             base #(int, 5)::current(), abstract_handle.get_value(),
             external_object.get(), cast_interface, cast_failure,
             retained_value + cast_null + (retained == null));
  end
endmodule

// CHECK: 10 6 3 12 8 14 1 0 11
