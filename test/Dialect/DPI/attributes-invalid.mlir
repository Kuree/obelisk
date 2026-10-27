// RUN: talos-opt %s --split-input-file --verify-diagnostics

// expected-error @+1 {{has undeclared attribute 'dpi.is_task'}}
func.func private @undeclared() attributes {dpi.is_task = true}

// -----

// expected-error @+1 {{attribute 'dpi.c_identifier' has the wrong payload kind}}
func.func private @wrong_kind() attributes {dpi.c_identifier = 3 : i32}

// -----

// expected-error @+1 {{attribute 'dpi.import_id' must be a nonzero 32-bit identity}}
func.func private @zero_id() attributes {dpi.import_id = 0 : i32}

// -----

// expected-error @+1 {{attribute 'dpi.abi_signature' must contain only #dpi.abi entries}}
func.func private @untyped_signature() attributes {
  dpi.abi_signature = [1 : i32]}

// -----

// expected-error @+1 {{attribute 'dpi.aggregate_layouts' must have one entry per ABI signature entry}}
func.func private @layout_count() attributes {
  dpi.abi_signature = [#dpi.abi<kind = int, direction = input, width = 32,
                                fourState = false, isSigned = true>],
  dpi.aggregate_layouts = [unit, unit]}

// -----

// expected-error @+1 {{attribute 'dpi.logical_inputs' exceeds the ABI signature}}
func.func private @logical_inputs() attributes {
  dpi.abi_signature = [], dpi.logical_inputs = 1 : i32}

// -----

// expected-error @+1 {{attribute 'dpi.elided_inputs' must be strictly increasing formal indices}}
func.func private @elided() attributes {dpi.elided_inputs = array<i64: 2, 1>}

// -----

// expected-error @+1 {{attribute 'dpi.has_exports' belongs on a module}}
func.func private @exports() attributes {dpi.has_exports}

// -----

func.func private @abi() attributes {
  // expected-error @+1 {{DPI ABI category requires width 32}}
  layout = #dpi.abi<kind = int, direction = input, width = 8,
                    fourState = false, isSigned = true>}
