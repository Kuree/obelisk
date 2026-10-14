// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

// expected-error @+1 {{has undeclared attribute 'native.instance_size'}}
func.func private @undeclared() attributes {native.instance_size = 8 : i64}

// -----

// expected-error @+1 {{attribute 'native.partition' has the wrong payload kind}}
func.func private @wrong_kind() attributes {native.partition = 1 : i32}

// -----

// expected-error @+1 {{attribute 'native.debug_timing' belongs on a module}}
func.func private @option() attributes {native.debug_timing}

// -----

// expected-error @+1 {{attribute 'native.physical_partition_manifest' must contain only #native.partition entries}}
module attributes {native.physical_partition_manifest = [{id = "primary"}]} {}

// -----

// expected-error @+1 {{attribute 'native.two_state_block_unknowns' must contain one index array per block}}
func.func private @blocks() attributes {
  native.two_state_block_unknowns = [[0]]}

// -----

func.func private @partition() attributes {
  // expected-error @+1 {{native partition members, imports and exports must be symbols}}
  native.partition_manifest = [#native.partition<id = "p", members = ["f"],
      imports = [], exports = [], dependencies = []>]}

// -----

func.func private @frame() attributes {
  // expected-error @+1 {{native frame alignment must be a power of two}}
  native.frame = #native.frame<size = 8, alignment = 3, checksum = 0,
                               continuations = [], fields = []>}

// -----

func.func private @site() attributes {
  // expected-error @+1 {{native frame site identity has four words}}
  inputs = #native.frame_site<identity = [1], types = []>}
