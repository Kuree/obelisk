// RUN: not obelisk -emit-slang %s -o /dev/null 2>&1 | FileCheck %s

module invalid_parallel_polarity(
    input wire lhs, input wire rhs, output wire destination);
  or (destination, lhs, rhs);
  specify
    // IEEE 1800-2017 30.4.1 permits exactly one input and one output terminal
    // for a parallel connection. Polarity does not turn this nonstandard
    // multi-input `=>` spelling into a full connection.
    (lhs, rhs +=> destination) = 1;
  endspecify
endmodule

// CHECK: error: cannot specify multiple terminals for parallel path connection
