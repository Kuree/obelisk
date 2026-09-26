// RUN: obelisk --compile-threads=1 %s -o %t.native
// RUN: %t.native | FileCheck %s

module native_name_shadowing;
  typedef struct { logic valid; logic       index; } small_t;
  typedef struct { logic valid; logic [4:0] index; } big_t;

  function automatic small_t make_small(logic value);
    return '{1'b1, value};
  endfunction

  function automatic logic [4:0] shadowed_type(logic value);
    big_t temporary;
    begin
      small_t temporary;
      temporary = make_small(value);
    end
    temporary.index = 5'd19;
    return temporary.index;
  endfunction

  function automatic int shadowed_loop(int start);
    int result = 0;
    for (int index = 0; index < 2; index++)
      result += index;
    for (int index = start; index < 5; index++)
      result += index;
    return result;
  endfunction

  initial begin
    $display("type=%0d loop=%0d", shadowed_type(1'b1), shadowed_loop(3));
    // CHECK: type=19 loop=8
  end
endmodule
