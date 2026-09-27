// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module attributes {
  simulation.mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false,
    isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>],
    childNames = []>,
  simulation.open = #simulation.vpi_type<kind = unpacked_open_array,
    isSigned = false, isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>],
    childNames = []>,
  simulation.process = #simulation.vpi_type<kind = process, isSigned = false,
    isFourState = false, range = [], children = [], childNames = []>,
  simulation.semaphore = #simulation.vpi_type<kind = semaphore,
    isSigned = false, isFourState = false, range = [], children = [],
    childNames = []>,
  simulation.test = #simulation.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31, 0], children = [], childNames = []>
} {}

// -----

// A packed array has the signedness of its packed element type.
// expected-error @+2 {{VPI fixed-array flags contradict its element or packing}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = packed_array,
    isSigned = true, isFourState = false, range = [3, 0],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

module {
  // expected-error @+1 {{does not normalize to executable type '!simulation.class_handle<@class_a>'}}
  simulation.storage.decl 0 in 0 : !simulation.class_handle<@class_a> design {
    vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
      isFourState = false, symbol = @class_b, range = [], children = [],
      childNames = []>
  }
}

// -----

// A zero-width packed aggregate was previously accepted and could divide by
// zero when nested below a packed array during declaration verification.
// expected-error @+2 {{packed VPI aggregate must have a nonzero bit width}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = packed_struct,
    isSigned = false, isFourState = false, name = "empty_t", range = [],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = ["member"], isTagged = false, isSoft = false,
    bitWidth = 0 : i64, selectableWidth = 0 : i64,
    bitstreamWidth = 0 : i64, tagBits = 0 : i64,
    childOrdinals = [0], childPackedOffsets = [0]>
} {}

// -----

// expected-error @+2 {{two-state VPI semantic type has a four-state flag}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = bit, isSigned = false,
    isFourState = true, range = [0, 0], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{four-state VPI semantic type lacks its flag}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = logic, isSigned = false,
    isFourState = false, range = [0, 0], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI queue semantic type requires bound metadata}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = queue, isSigned = false,
    isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI associative-array semantic type requires wildcard metadata}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = assoc_array,
    isSigned = false, isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>,
      #simulation.vpi_type<kind = string, isSigned = false,
        isFourState = false, range = [], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI aggregate semantic type requires complete layout metadata}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = packed_struct,
    isSigned = false, isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = ["member"]>
} {}

// -----

module {
  // expected-error @+1 {{does not normalize to executable type 'i8'}}
  simulation.storage.decl 0 in 0 : i8 design {vpi_type = #simulation.vpi_type<
    kind = bit, isSigned = false, isFourState = false, range = [0, 0],
    children = [], childNames = []>}
}

// -----

module {
  // expected-error @+1 {{does not normalize to executable type '!simulation.queue<i1, 3>'}}
  simulation.storage.decl 0 in 0 : !simulation.queue<i1, 3> design {
    vpi_type = #simulation.vpi_type<kind = queue, isSigned = false,
      isFourState = false, range = [],
      children = [#simulation.vpi_type<kind = bit, isSigned = false,
        isFourState = false, range = [0, 0], children = [], childNames = []>],
      childNames = [], queueBound = 4 : i64>}
}

// -----

// expected-error @+2 {{VPI semantic type kind int requires a source range}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type has an invalid named-type identity}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = string, isSigned = false,
    isFourState = false, name = "not-a-named-type", range = [], children = [],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind class requires a named-type identity}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = class, isSigned = false,
    isFourState = false, range = [], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type range must be empty or contain one left/right pair}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31], children = [], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind int requires 0 child type(s)}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = int, isSigned = true,
    isFourState = false, range = [31, 0],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI enum semantic type requires a name}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = enum, isSigned = false,
    isFourState = true, range = [],
    children = [#simulation.vpi_type<kind = logic, isSigned = false,
      isFourState = true, range = [1, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type kind assoc_array requires 2 child type(s)}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = assoc_array, isSigned = false,
    isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = int, isSigned = true,
      isFourState = false, range = [31, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI aggregate semantic type requires one name per field}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = packed_struct, isSigned = false,
    isFourState = true, range = [],
    children = [#simulation.vpi_type<kind = logic, isSigned = false,
      isFourState = true, range = [0, 0], children = [], childNames = []>],
    childNames = []>
} {}

// -----

// expected-error @+2 {{VPI aggregate field randomization types must be vpiNotRand, vpiRand, or vpiRandC}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = packed_struct,
    isSigned = false, isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = ["member"], isTagged = false, isSoft = false,
    bitWidth = 1 : i64, selectableWidth = 1 : i64,
    bitstreamWidth = 1 : i64, tagBits = 0 : i64, childOrdinals = [0],
    childPackedOffsets = [0], childRandTypes = [4]>
} {}

// -----

// expected-error @+2 {{only VPI aggregate semantic types may name child fields}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = queue, isSigned = false,
    isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = ["element"]>
} {}

// -----

// expected-error @+2 {{VPI semantic type children must all be semantic type attributes}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = queue, isSigned = false,
    isFourState = false, range = [], children = [42 : i32], childNames = []>
} {}

// -----

// expected-error @+2 {{VPI semantic type child names must all be strings}}
module attributes {
  simulation.test = #simulation.vpi_type<kind = packed_struct, isSigned = false,
    isFourState = false, range = [],
    children = [#simulation.vpi_type<kind = bit, isSigned = false,
      isFourState = false, range = [0, 0], children = [], childNames = []>],
    childNames = [42 : i32]>
} {}
