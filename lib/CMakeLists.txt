add_subdirectory(Dialect)
add_subdirectory(Coverage)
add_subdirectory(Analysis)
add_subdirectory(Solver)
add_subdirectory(Conversion)
add_subdirectory(Frontend)

include(${PROJECT_SOURCE_DIR}/cmake/ObeliskPrecompiledHeaders.cmake)

set(_simulation_ops
  "${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Simulation/SimulationOps.h")
obelisk_precompile_headers(ObeliskSimulationIR "${_simulation_ops}")
obelisk_precompile_headers(ObeliskSimulationAnalysis "${_simulation_ops}")
obelisk_precompile_headers(ObeliskSimulationTransforms
  "${_simulation_ops}" <mlir/Pass/Pass.h>)
obelisk_precompile_headers(ObeliskSimulationToBytecode "${_simulation_ops}")
obelisk_precompile_headers(ObeliskToSimulation
  "${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Obelisk/ObeliskOps.h"
  "${_simulation_ops}")
obelisk_precompile_headers(ObeliskSimulationToLLVMCoroutine
  "${_simulation_ops}"
  <mlir/Dialect/LLVMIR/LLVMDialect.h>
  <mlir/Transforms/DialectConversion.h>)
unset(_simulation_ops)
