// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module attributes {
  obelisk_sim.mailbox = #obelisk_sim.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [],
    children = [#obelisk_sim.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>],
    childNames = []>,
  obelisk_sim.open = #obelisk_sim.vpi_type<kind = unpacked_open_array,
    isSigned = false, isFourState = false, range = [],
    children = [#obelisk_sim.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>],
    childNames = []>,
  obelisk_sim.process = #obelisk_sim.vpi_type<kind = process, isSigned = false,
    isFourState = false, range = [], children = [], childNames = []>,
  obelisk_sim.semaphore = #obelisk_sim.vpi_type<kind = semaphore,
    isSigned = false, isFourState = false, range = [], children = [],
    childNames = []>,
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31, 0], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind int requires a source range}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type has an invalid named-type identity}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = string, isSigned = false,
    isFourState = false, name = "not-a-named-type", range = [], children = [],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind class requires a named-type identity}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = class, isSigned = false,
    isFourState = false, range = [], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type range must be empty or contain one left/right pair}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind int requires 0 child type(s)}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31, 0],
    children = [#obelisk_sim.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI enum semantic type requires a name}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
    isFourState = true, range = [],
    children = [#obelisk_sim.vpi_type<kind = logic, isSigned = false,
      isFourState = true, range = [1, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind assoc_array requires 2 child type(s)}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = assoc_array, isSigned = false,
    isFourState = false, range = [],
    children = [#obelisk_sim.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI aggregate semantic type requires one name per field}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = packed_struct, isSigned = false,
    isFourState = true, range = [],
    children = [#obelisk_sim.vpi_type<kind = logic, isSigned = false,
      isFourState = true, range = [0, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{only VPI aggregate semantic types may name child fields}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = queue, isSigned = false,
    isFourState = false, range = [],
    children = [#obelisk_sim.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = ["element"]>
} {}

// -----

// expected-error @+2 {{VPI semantic type children must all be semantic type attributes}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = queue, isSigned = false,
    isFourState = false, range = [], children = [42 : i32], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type child names must all be strings}}
module attributes {
  obelisk_sim.test = #obelisk_sim.vpi_type<kind = packed_struct, isSigned = false,
    isFourState = false, range = [],
    children = [#obelisk_sim.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = [42 : i32]>
} {}
