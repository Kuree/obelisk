add_subdirectory(Dialect)
add_subdirectory(Coverage)
add_subdirectory(Analysis)
add_subdirectory(Solver)
add_subdirectory(Conversion)
add_subdirectory(Frontend)
add_subdirectory(LLVM)

include(${PROJECT_SOURCE_DIR}/cmake/TalosPrecompiledHeaders.cmake)

set(_simulation_ops
  "${PROJECT_SOURCE_DIR}/include/talos/Dialect/Simulation/SimulationOps.h")
talos_precompile_headers(TalosSimulationIR "${_simulation_ops}")
talos_precompile_headers(TalosSimulationAnalysis "${_simulation_ops}")
talos_precompile_headers(TalosSimulationTransforms
  "${_simulation_ops}" <mlir/Pass/Pass.h>)
talos_precompile_headers(TalosSimulationToBytecode "${_simulation_ops}")
talos_precompile_headers(TalosToSimulation
  "${PROJECT_SOURCE_DIR}/include/talos/Dialect/Talos/TalosOps.h"
  "${_simulation_ops}")
talos_precompile_headers(TalosSimulationToLLVMCoroutine
  "${_simulation_ops}"
  <mlir/Dialect/LLVMIR/LLVMDialect.h>
  <mlir/Transforms/DialectConversion.h>)
talos_precompile_headers(TalosSimulationToSchedule "${_simulation_ops}")
talos_precompile_headers(TalosSimulationToScheduleNative
  "${_simulation_ops}" <mlir/Dialect/LLVMIR/LLVMDialect.h>)
talos_precompile_headers(TalosScheduleTransforms
  "${_simulation_ops}" <mlir/Pass/Pass.h>)
talos_precompile_headers(TalosScheduleNativeTransforms
  "${_simulation_ops}" <mlir/Dialect/LLVMIR/LLVMDialect.h> <mlir/Pass/Pass.h>)
unset(_simulation_ops)
