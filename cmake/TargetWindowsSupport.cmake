# Build the simulation runtime with the same SDK Clang used by the compiler.
# COFF links use the host Windows SDK/UCRT and Visual C++ runtime libraries.
set(_talos_windows_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(TALOS_TARGET_RUNTIME_ARCHIVE
  "${_talos_windows_runtime_dir}/libtalos_rt.a")
set(TALOS_TARGET_RUNTIME_LTO_ARCHIVE
  "${_talos_windows_runtime_dir}/libtalos_rt_lto.a")
set(TALOS_NATIVE_SUPPORT_DIR
  "${CMAKE_BINARY_DIR}/lib/talos/targets/${TALOS_TARGET_TRIPLE}")
set(_talos_windows_runtime_sources
  ABI Bytecode Containers Coverage CoverageBlockEvents CoverageDatabase
  DesignBytecode DesignBytecodeImage DesignBytecodeIntrinsics DesignBytecodeLogic
  DesignBytecodeNets DesignBytecodeObservers DesignBytecodeRoots DesignDatabase
  DPI FileIO Format ManagedHeap Plusargs Process ProcessAllocation ProcessAOT
  ProcessNativeState ProcessNBA ProcessObservers ProcessSignals ProcessState
  ProcessTable ProcessTransitions ProcessValidation Random RandSolve RandSamplePlan
  RandReal RandSolveWide Runtime Sampled StochasticQueue System VCD VPI VPIValueLayout
  ScanFormat DynamicScanBytecode ContainerBitstream RecursiveBitstream
  ContainerBitstreamBytecode ClassBitstream ClassBitstreamBytecode DPIExport
  DPIExportBytecode DPIOpenArray DPIAggregate)
set(_talos_windows_runtime_headers
  "${CMAKE_BINARY_DIR}/include/talos/Reflection/DesignReflectionLayout.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Reflection/VPIObjectModel.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Coverage/CoverageFormatDecls.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Coverage/CoverageFormatParser.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Coverage/CoverageFormatSerializer.h.inc")
set(_talos_windows_runtime_definitions -D_CRT_SECURE_NO_WARNINGS)
if(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _talos_windows_runtime_definitions
    -DTALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
# COFF LTO treats undefined weak IR symbols as archive-extracting references.
# Keep those callers as native COFF so unencoded features stay unlinked. The
# rest of the runtime still participates in Full LTO with the generated design.
set(_talos_windows_native_dispatchers
  DesignBytecodeIntrinsics ProcessSignals Runtime
  ContainerBitstreamBytecode DPIExport)
foreach(flavor IN ITEMS native lto)
  set(objects)
  set(lto_flags)
  if(flavor STREQUAL "lto")
    set(lto_flags -flto=full -funified-lto)
  endif()
  foreach(source IN LISTS _talos_windows_runtime_sources)
    if(flavor STREQUAL "lto" AND source IN_LIST _talos_windows_native_dispatchers)
      list(APPEND objects "${_talos_windows_runtime_dir}/native/${source}.obj")
      continue()
    endif()
    set(object "${_talos_windows_runtime_dir}/${flavor}/${source}.obj")
    list(APPEND objects "${object}")
    add_custom_command(
      OUTPUT "${object}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_talos_windows_runtime_dir}/${flavor}"
      COMMAND ${CMAKE_CXX_COMPILER_LAUNCHER}
        "${TALOS_LLVM_TOOLS_DIR}/clang++.exe"
        --target=${TALOS_TARGET_TRIPLE} -std=c++17 -O3
        -fms-runtime-lib=dll -ffunction-sections -fdata-sections ${lto_flags}
        ${_talos_windows_runtime_definitions}
        "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=/talos"
        -I "${PROJECT_SOURCE_DIR}/runtime/include"
        -I "${PROJECT_SOURCE_DIR}/include"
        -I "${CMAKE_BINARY_DIR}/include"
        -isystem "${TALOS_SLANG_SOURCE_DIR}/external/ieee1800"
        -MD -MF "${object}.d" -MQ "${object}"
        -c "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp" -o "${object}"
      DEPFILE "${object}.d"
      DEPENDS "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp"
        ${_talos_windows_runtime_headers}
      VERBATIM)
  endforeach()
  if(flavor STREQUAL "lto")
    set(archive "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}")
  else()
    set(archive "${TALOS_TARGET_RUNTIME_ARCHIVE}")
  endif()
  add_custom_command(
    OUTPUT "${archive}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${archive}"
    COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-ar.exe" rcs "${archive}" ${objects}
    COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-ar.exe" qLs
      "${archive}" "${TALOS_COMPILER_RT_BUILTINS}"
    DEPENDS ${objects} "${TALOS_COMPILER_RT_BUILTINS}"
    VERBATIM)
endforeach()
add_custom_target(talos_target_runtime
  DEPENDS "${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}")
set(_talos_windows_support_stamp "${CMAKE_BINARY_DIR}/windows-native-support.complete")
add_custom_command(
  OUTPUT "${_talos_windows_support_stamp}"
  BYPRODUCTS "${TALOS_NATIVE_SUPPORT_DIR}/.complete"
    "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt.a"
    "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt_lto.a"
    "${TALOS_NATIVE_SUPPORT_DIR}/talos-pi.def"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/LICENSE.TXT"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/CREDITS.TXT"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/talos/LICENSE"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TALOS_NATIVE_SUPPORT_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${TALOS_TARGET_RUNTIME_ARCHIVE}" "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${TALOS_NATIVE_SUPPORT_DIR}"
  COMMAND "${CMAKE_COMMAND}"
    "-DLLVM_NM=${TALOS_LLVM_TOOLS_DIR}/llvm-nm.exe"
    "-DRUNTIME=${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "-DOUTPUT=${TALOS_NATIVE_SUPPORT_DIR}/talos-pi.def"
    -P "${PROJECT_SOURCE_DIR}/cmake/StageWindowsPI.cmake"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/talos"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/LICENSE.TXT"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/CREDITS.TXT"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${TALOS_NATIVE_SUPPORT_DIR}/licenses/talos/LICENSE"
  COMMAND "${CMAKE_COMMAND}" -E touch "${TALOS_NATIVE_SUPPORT_DIR}/.complete"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_talos_windows_support_stamp}"
  DEPENDS "${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${PROJECT_SOURCE_DIR}/cmake/StageWindowsPI.cmake"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
    "${PROJECT_SOURCE_DIR}/LICENSE"
  VERBATIM)
add_custom_target(talos_native_support
  DEPENDS "${_talos_windows_support_stamp}")
add_dependencies(talos_native_support talos_target_runtime)
