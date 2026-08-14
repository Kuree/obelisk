// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module attributes {
  // expected-error @below {{random-value reference requires a nonzero width}}
  test = #obelisk_sim.random_value_reference<
    kind = object_field, target = @value, low = 0, width = 0>
} {}

// -----

module attributes {
  // expected-error @below {{object constraint-block reference requires an index from 0 through 63}}
  test = #obelisk_sim.random_constraint_block_reference<
    kind = object_block, index = 64 : i32>
} {}

// -----

module attributes {
  // expected-error @below {{object constraint-block reference cannot name storage}}
  test = #obelisk_sim.random_constraint_block_reference<
    kind = object_block, index = 0 : i32, storage = 1 : i64>
} {}

// -----

module attributes {
  // expected-error @below {{storage constraint-block reference cannot name an object index}}
  test = #obelisk_sim.random_constraint_block_reference<
    kind = storage, index = 0 : i32, storage = 1 : i64>
} {}

// -----

module attributes {
  // expected-error @below {{storage constraint-block reference requires a nonnegative 64-bit descriptor ID}}
  test = #obelisk_sim.random_constraint_block_reference<kind = storage>
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
