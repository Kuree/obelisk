// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o3.native.out

module native_for_loop;
  timeunit 1ns;
  timeprecision 1ns;

  integer i;
  integer j;
  integer sequence_digits;
  integer continue_sum;
  integer continue_step;
  integer break_i;
  integer break_j;
  integer condition_digits;
  integer condition_i;
  integer forever_count;
  integer declaration_digits;
  integer delayed_digits;
  integer nested_digits;
  integer empty_body_i;
  integer unknown_iterations;
  integer function_result;
  logic unknown_condition;

  function integer accumulate;
    integer n;
    begin
      accumulate = 0;
      for (n = 1; n <= 3; n++)
        accumulate = accumulate + n;
    end
  endfunction

  initial begin
    // Initializers and steps are both ordered expression lists. The second
    // expression observes the first expression's update.
    sequence_digits = 0;
    for (i = 1, j = i + 1; i <= 3; i = i + 1, j = j + i)
      sequence_digits = sequence_digits * 10 + j;

    // A continue transfers to the step list, not directly to the condition.
    continue_sum = 0;
    for (i = 0, j = 0; i < 5; i = i + 1, j = j + 10) begin
      if (i == 1)
        continue;
      if (i == 3)
        continue;
      continue_sum = continue_sum + i + j;
    end
    continue_step = j;

    // An absent condition is true. A break bypasses every step expression.
    for (i = 0, j = 10;; i = i + 1, j = j + 2)
      if (i == 3)
        break;
    break_i = i;
    break_j = j;

    // The condition is evaluated before every iteration, even without steps.
    i = 0;
    condition_digits = 0;
    for (; i++ < 3;)
      condition_digits = condition_digits * 10 + i;
    condition_i = i;

    // All three control clauses may be absent.
    forever_count = 0;
    for (;;) begin
      forever_count = forever_count + 1;
      if (forever_count == 2)
        break;
    end

    // Loop-variable declarations are initialized in source order and remain
    // scoped to the loop while the loop node itself has no expression init.
    declaration_digits = 0;
    for (int x = 1, y = x + 2; x <= 3;
         x = x + 1, y = y + x)
      declaration_digits = declaration_digits * 10 + y;

    // Suspension in the body must resume into the same iteration, and
    // continue after a suspension must still execute the step.
    delayed_digits = 0;
    for (i = 0; i < 3; i = i + 1) begin
      #1;
      delayed_digits = delayed_digits * 10 + i;
      if (i == 1)
        continue;
      delayed_digits = delayed_digits + 5;
    end

    // Nested continues select the innermost loop's step block.
    nested_digits = 0;
    for (i = 0; i < 2; i = i + 1)
      for (j = 0; j < 3; j = j + 1) begin
        if (j == 1)
          continue;
        nested_digits = nested_digits * 10 + i * 3 + j;
      end

    for (empty_body_i = 0; empty_body_i < 2; empty_body_i++);

    // As in other procedural conditions, X is not true.
    unknown_condition = 1'bx;
    unknown_iterations = 0;
    for (i = 0; unknown_condition; i = i + 1)
      unknown_iterations = unknown_iterations + 1;

    function_result = accumulate();

    $display(
        "for sequence=%0d continue=%0d/%0d break=%0d/%0d condition=%0d/%0d forever=%0d declaration=%0d delay=%0d time=%0t nested=%0d empty=%0d unknown=%0d function=%0d",
        sequence_digits, continue_sum, continue_step, break_i, break_j,
        condition_digits, condition_i, forever_count, declaration_digits,
        delayed_digits, $time, nested_digits, empty_body_i,
        unknown_iterations, function_result);
  end
endmodule

// CHECK: for sequence=247 continue=66/50 break=3/16 condition=123/4 forever=2 declaration=358 delay=517 time=3 nested=235 empty=2 unknown=0 function=6
