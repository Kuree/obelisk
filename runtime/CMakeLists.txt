find_package(Threads REQUIRED)

set(_talos_runtime_common_sources
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
  lib/RandSamplePlan.cpp
  lib/RandReal.cpp
  lib/RandSolveWide.cpp
  lib/Runtime.cpp
  lib/Sampled.cpp
  lib/System.cpp
  lib/StochasticQueue.cpp
  lib/VCD.cpp
  lib/VPI.cpp
  lib/VPIValueLayout.cpp
  lib/VPIObjectModelABI.cpp
)
set(_talos_runtime_cold_tail_sources
  # Keep feature-only services after every common runtime object. Native links
  # additionally group their functions in .talos.feature.text; wasm uses
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
add_library(talos_rt STATIC
  ${_talos_runtime_common_sources}
  ${_talos_runtime_cold_tail_sources}
)

add_library(TalosRuntime ALIAS talos_rt)
add_dependencies(talos_rt
  TalosCoverageFormatDeclsIncGen
  TalosCoverageFormatParserIncGen
  TalosCoverageFormatSerializerIncGen
  TalosDesignReflectionIncGen
  TalosVPIObjectModelIncGen)

target_include_directories(talos_rt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(talos_rt SYSTEM PRIVATE
  ${TALOS_SLANG_SOURCE_DIR}/external/ieee1800
)
target_include_directories(talos_rt PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_BINARY_DIR}/include"
)

target_compile_features(talos_rt PUBLIC cxx_std_17)
target_link_libraries(talos_rt PUBLIC Threads::Threads)
if(TALOS_COMPILER_RT_BUILTINS)
  # Flatten compiler-rt into our archive. Static consumers need only talos_rt;
  # archive members are still extracted on demand by the linker.
  add_custom_command(TARGET talos_rt POST_BUILD
    COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-ar${_talos_host_executable_suffix}" qLs
      "$<TARGET_FILE:talos_rt>" "${TALOS_COMPILER_RT_BUILTINS}"
    VERBATIM)
  # LINK_DEPENDS is ignored for static libraries. A cheap ABI object carries
  # the dependency so replacing the SDK's builtins also recreates this archive.
  set_property(SOURCE lib/ABI.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${TALOS_COMPILER_RT_BUILTINS}")
endif()
if(APPLE)
  # Host-side tests also consume the runtime as a normal static library.
  # Give their compiler driver the same bounded weak-import allowance used
  # by the in-process Mach-O linker for generated simulations.
  set(_talos_macos_weak_imports "${CMAKE_CURRENT_BINARY_DIR}/weak-imports")
  add_custom_command(TARGET talos_rt POST_BUILD
    BYPRODUCTS "${_talos_macos_weak_imports}.txt"
               "${_talos_macos_weak_imports}.rsp"
    COMMAND "${CMAKE_COMMAND}"
      "-DLLVM_NM=${TALOS_LLVM_TOOLS_DIR}/llvm-nm"
      "-DRUNTIME=$<TARGET_FILE:talos_rt>"
      "-DOUTPUT_PREFIX=${_talos_macos_weak_imports}"
      -P "${PROJECT_SOURCE_DIR}/cmake/MacOSWeakImports.cmake"
    VERBATIM)
  target_link_options(talos_rt INTERFACE "@${_talos_macos_weak_imports}.rsp")
  set_property(TARGET talos_rt APPEND PROPERTY INTERFACE_LINK_DEPENDS
    "${_talos_macos_weak_imports}.rsp")
  set_property(SOURCE lib/ABI.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${PROJECT_SOURCE_DIR}/cmake/MacOSWeakImports.cmake")
endif()
if(WIN32)
  # Consumers of this archive resolve VPI entry points statically, so the
  # canonical IEEE headers must not turn their calls into DLL imports.
  target_compile_definitions(talos_rt PUBLIC PLI_DLLISPEC= PLI_DLLESPEC=)
  target_compile_definitions(talos_rt INTERFACE DPI_DLLISPEC=)
endif()
if(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  target_compile_definitions(talos_rt PRIVATE
    TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
set_target_properties(talos_rt PROPERTIES
  OUTPUT_NAME talos_rt
  POSITION_INDEPENDENT_CODE ON
)

if(TALOS_INCLUDE_TESTS)
  add_subdirectory(unittests)
endif()
