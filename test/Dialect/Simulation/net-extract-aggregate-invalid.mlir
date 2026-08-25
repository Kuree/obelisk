// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @misaligned(
      %net: !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = obelisk_sim.net.extract %net from 1 : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
    return
  }
}

// -----

module {
  func.func @out_of_range(
      %net: !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = obelisk_sim.net.extract %net from 4 : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
    return
  }
}

// -----

module {
  func.func @wrong_type(
      %net: !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = obelisk_sim.net.extract %net from 0 : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.net<!obelisk_sim.logic<1>>
    return
  }
}

// -----

module {
  func.func @negative(
      %net: !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>) {
    // expected-error @+1 {{aggregate selection must identify one exact unpacked array element}}
    %bad = obelisk_sim.net.extract %net from -1 : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
    return
  }
}
