add_subdirectory(Dialect)
add_subdirectory(Coverage)
add_subdirectory(Analysis)
add_subdirectory(Solver)
add_subdirectory(Conversion)
add_subdirectory(Frontend)

include(${PROJECT_SOURCE_DIR}/cmake/ObeliskPrecompiledHeaders.cmake)

set(_obelisk_sim_ops
  "${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Simulation/SimulationOps.h")
obelisk_precompile_headers(ObeliskSimulationIR "${_obelisk_sim_ops}")
obelisk_precompile_headers(ObeliskSimulationAnalysis "${_obelisk_sim_ops}")
obelisk_precompile_headers(ObeliskSimulationTransforms
  "${_obelisk_sim_ops}" <mlir/Pass/Pass.h>)
obelisk_precompile_headers(ObeliskSimulationToBytecode "${_obelisk_sim_ops}")
obelisk_precompile_headers(ObeliskToSimulation
  "${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Obelisk/ObeliskOps.h"
  "${_obelisk_sim_ops}")
obelisk_precompile_headers(ObeliskSimulationToLLVMCoroutine
  "${_obelisk_sim_ops}"
  <mlir/Dialect/LLVMIR/LLVMDialect.h>
  <mlir/Transforms/DialectConversion.h>)
unset(_obelisk_sim_ops)
