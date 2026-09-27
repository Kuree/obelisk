// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @misaligned(
      %net: !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = simulation.net.extract %net from 1 : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
    return
  }
}

// -----

module {
  func.func @out_of_range(
      %net: !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = simulation.net.extract %net from 4 : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
    return
  }
}

// -----

module {
  func.func @wrong_type(
      %net: !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = simulation.net.extract %net from 0 : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.net<!simulation.logic<1>>
    return
  }
}

// -----

module {
  func.func @negative(
      %net: !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = simulation.net.extract %net from -1 : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
    return
  }
}
