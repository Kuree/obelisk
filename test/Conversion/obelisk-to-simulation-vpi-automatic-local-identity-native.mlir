// Reuse the authored identity regression for both native optimization profiles.
// RUN: obelisk-opt %S/obelisk-to-simulation-vpi-automatic-local-identity.mlir '--lower-obelisk-to-sim=vpi=full instrument-vpi-frames=true opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %S/obelisk-to-simulation-vpi-automatic-local-identity.mlir '--lower-obelisk-to-sim=vpi=off opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %S/obelisk-to-simulation-vpi-automatic-local-identity.mlir '--lower-obelisk-to-sim=vpi=full instrument-vpi-frames=true opt-level=3' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %S/obelisk-to-simulation-vpi-automatic-local-identity.mlir '--lower-obelisk-to-sim=vpi=off opt-level=3' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
