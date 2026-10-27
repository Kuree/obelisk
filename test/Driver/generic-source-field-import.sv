// RUN: talos -emit-slang %s -o /dev/null
// RUN: talos -emit-talos %s -o /dev/null

// Importing unused generic source inventory must not bind dependent fields,
// initializer expressions, or typedef targets merely to preserve declarations.
class G #(type T, int N = 3);
  typedef int Word;
  typedef Word Count;
  typedef T Dependent;
  rand int count;
  local static const Count fixed_count = 7;
  protected bit flag;
  event wake;
  T data;
  T dependent_array[N];
  function T dependent_method(); return data; endfunction
endclass
module top; endmodule
