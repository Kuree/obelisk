find_package(Threads REQUIRED)

set(_obelisk_runtime_common_sources
  lib/ABI.cpp
  lib/Bytecode.cpp
  lib/DesignBytecode.cpp
  lib/DesignBytecodeImage.cpp
  lib/DesignBytecodeIntrinsics.cpp
  lib/DesignBytecodeLogic.cpp
  lib/DesignBytecodeNets.cpp
  lib/DesignBytecodeObservers.cpp
  lib/DesignBytecodeRoots.cpp
  lib/DesignDatabase.cpp
  lib/DesignReflectionABI.cpp
  lib/DPI.cpp
  lib/FileIO.cpp
  lib/Format.cpp
  lib/ManagedHeap.cpp
  lib/Containers.cpp
  lib/Coverage.cpp
  lib/CoverageBlockEvents.cpp
  lib/CoverageDatabase.cpp
  lib/Process.cpp
  lib/ProcessTable.cpp
  lib/ProcessAllocation.cpp
  lib/ProcessAOT.cpp
  lib/ProcessNBA.cpp
  lib/ProcessNativeState.cpp
  lib/ProcessObservers.cpp
  lib/ProcessSignals.cpp
  lib/ProcessState.cpp
  lib/ProcessTransitions.cpp
  lib/ProcessValidation.cpp
  lib/Plusargs.cpp
  lib/Random.cpp
  lib/RandSolve.cpp
  lib/RandSolveWide.cpp
  lib/Runtime.cpp
  lib/Sampled.cpp
  lib/System.cpp
  lib/StochasticQueue.cpp
  lib/VCD.cpp
  lib/VPI.cpp
  lib/VPIObjectModelABI.cpp
)
set(_obelisk_runtime_cold_tail_sources
  # Keep feature-only services after every common runtime object. Native links
  # additionally group their functions in .obelisk.feature.text; wasm uses
  # the portable cold attribute in its single code section.
  lib/ScanFormat.cpp
  lib/DynamicScanBytecode.cpp
  lib/ContainerBitstream.cpp
  lib/RecursiveBitstream.cpp
  lib/ContainerBitstreamBytecode.cpp
  lib/ClassBitstream.cpp
  lib/ClassBitstreamBytecode.cpp
  lib/DPIExport.cpp
  lib/DPIExportBytecode.cpp
  lib/DPIOpenArray.cpp
  lib/DPIAggregate.cpp
)
add_library(obelisk_rt STATIC
  ${_obelisk_runtime_common_sources}
  ${_obelisk_runtime_cold_tail_sources}
)

add_library(ObeliskRuntime ALIAS obelisk_rt)
add_dependencies(obelisk_rt
  ObeliskCoverageFormatDeclsIncGen
  ObeliskCoverageFormatParserIncGen
  ObeliskCoverageFormatSerializerIncGen
  ObeliskDesignReflectionIncGen
  ObeliskVPIObjectModelIncGen)

target_include_directories(obelisk_rt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(obelisk_rt SYSTEM PRIVATE
  ${OBELISK_SLANG_SOURCE_DIR}/external/ieee1800
)
target_include_directories(obelisk_rt PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_BINARY_DIR}/include"
)

target_compile_features(obelisk_rt PUBLIC cxx_std_17)
target_link_libraries(obelisk_rt PUBLIC Threads::Threads)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  target_compile_definitions(obelisk_rt PRIVATE
    OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
set_target_properties(obelisk_rt PROPERTIES
  OUTPUT_NAME obelisk_rt
  POSITION_INDEPENDENT_CODE ON
)

if(OBELISK_INCLUDE_TESTS)
  add_subdirectory(unittests)
endif()
