// Reuse the authored identity regression for both native optimization profiles.
// RUN: talos-opt %S/talos-to-simulation-vpi-automatic-local-identity.mlir '--lower-talos-to-sim=vpi=full instrument-vpi-frames=true opt-level=0' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: talos-opt %S/talos-to-simulation-vpi-automatic-local-identity.mlir '--lower-talos-to-sim=vpi=off opt-level=0' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: talos-opt %S/talos-to-simulation-vpi-automatic-local-identity.mlir '--lower-talos-to-sim=vpi=full instrument-vpi-frames=true opt-level=3' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: talos-opt %S/talos-to-simulation-vpi-automatic-local-identity.mlir '--lower-talos-to-sim=vpi=off opt-level=3' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
