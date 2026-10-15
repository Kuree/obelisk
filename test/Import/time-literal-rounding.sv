// RUN: obelisk -emit-slang --std=1800-2017 %s | FileCheck %s --check-prefix=IEEE2017
// RUN: obelisk -emit-slang --std=1800-2023 %s | FileCheck %s --check-prefix=IEEE2023

// A time literal is a realtime value scaled to the current time unit.
// IEEE 1800-2017 5.8 also rounds it to the current time precision, half away
// from zero; IEEE 1800-2023 5.8 drops that rounding, so a literal finer than
// the precision keeps its exact value outside a delay.
module top;
  timeunit 10ns/1ns;
  realtime finer = 2.1ns;
  realtime half = 0.5ns;
endmodule

// IEEE2017: slang.expression.time_literal attributes {constant_value = "0.20000000000000001"
// IEEE2017: slang.expression.time_literal attributes {constant_value = "0.10000000000000001"

// IEEE2023: slang.expression.time_literal attributes {constant_value = "0.21000000000000002"
// IEEE2023: slang.expression.time_literal attributes {constant_value = "0.050000000000000003"
