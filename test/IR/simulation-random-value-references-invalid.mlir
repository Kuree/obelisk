// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module attributes {
  // expected-error @below {{random-value reference requires a nonzero width}}
  test = #obelisk_sim.random_value_reference<
    kind = object_field, target = @value, low = 0, width = 0>
} {}

// -----

module attributes {
  // expected-error @below {{random-value reference bit range overflows}}
  test = #obelisk_sim.random_value_reference<
    kind = object_field, target = @value,
    low = 18446744073709551615, width = 2>
} {}

// -----

module attributes {
  // expected-error @below {{object-field random-value reference requires a target}}
  test = #obelisk_sim.random_value_reference<
    kind = object_field, low = 0, width = 1>
} {}

// -----

module attributes {
  // expected-error @below {{object-field random-value reference cannot name storage}}
  test = #obelisk_sim.random_value_reference<
    kind = object_field, target = @value, storage = 1 : i64,
    low = 0, width = 1>
} {}

// -----

module attributes {
  // expected-error @below {{storage random-value reference cannot name an object field}}
  test = #obelisk_sim.random_value_reference<
    kind = storage, target = @value, storage = 1 : i64,
    low = 0, width = 1>
} {}

// -----

module attributes {
  // expected-error @below {{storage random-value reference requires a nonnegative 64-bit descriptor ID}}
  test = #obelisk_sim.random_value_reference<
    kind = storage, low = 0, width = 1>
} {}

// -----

module attributes {
  // expected-error @below {{storage random-value reference requires a nonnegative 64-bit descriptor ID}}
  test = #obelisk_sim.random_value_reference<
    kind = storage, storage = -1 : i64, low = 0, width = 1>
} {}
