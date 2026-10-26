# Build the simulation runtime with the same SDK Clang used by the compiler.
# COFF links use the host Windows SDK/UCRT and Visual C++ runtime libraries.
set(_obelisk_windows_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(OBELISK_TARGET_RUNTIME_ARCHIVE
  "${_obelisk_windows_runtime_dir}/libobelisk_rt.a")
set(OBELISK_TARGET_RUNTIME_LTO_ARCHIVE
  "${_obelisk_windows_runtime_dir}/libobelisk_rt_lto.a")
set(OBELISK_NATIVE_SUPPORT_DIR
  "${CMAKE_BINARY_DIR}/lib/obelisk/targets/${OBELISK_TARGET_TRIPLE}")
set(_obelisk_windows_runtime_sources
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
set(_obelisk_windows_runtime_headers
  "${CMAKE_BINARY_DIR}/include/obelisk/Reflection/DesignReflectionLayout.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Reflection/VPIObjectModel.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Coverage/CoverageFormatDecls.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Coverage/CoverageFormatParser.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Coverage/CoverageFormatSerializer.h.inc")
set(_obelisk_windows_runtime_definitions)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _obelisk_windows_runtime_definitions
    -DOBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
# COFF LTO treats undefined weak IR symbols as archive-extracting references.
# Keep those callers as native COFF so unencoded features stay unlinked. The
# rest of the runtime still participates in Full LTO with the generated design.
set(_obelisk_windows_native_dispatchers
  DesignBytecodeIntrinsics ProcessSignals Runtime
  ContainerBitstreamBytecode DPIExport)
foreach(flavor IN ITEMS native lto)
  set(objects)
  set(lto_flags)
  if(flavor STREQUAL "lto")
    set(lto_flags -flto=full -funified-lto)
  endif()
  foreach(source IN LISTS _obelisk_windows_runtime_sources)
    if(flavor STREQUAL "lto" AND source IN_LIST _obelisk_windows_native_dispatchers)
      list(APPEND objects "${_obelisk_windows_runtime_dir}/native/${source}.obj")
      continue()
    endif()
    set(object "${_obelisk_windows_runtime_dir}/${flavor}/${source}.obj")
    list(APPEND objects "${object}")
    add_custom_command(
      OUTPUT "${object}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_obelisk_windows_runtime_dir}/${flavor}"
      COMMAND "${OBELISK_LLVM_TOOLS_DIR}/clang++.exe"
        --target=${OBELISK_TARGET_TRIPLE} -std=c++17 -O3
        -fms-runtime-lib=dll -ffunction-sections -fdata-sections ${lto_flags}
        ${_obelisk_windows_runtime_definitions}
        "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=/obelisk"
        -I "${PROJECT_SOURCE_DIR}/runtime/include"
        -I "${PROJECT_SOURCE_DIR}/include"
        -I "${CMAKE_BINARY_DIR}/include"
        -isystem "${OBELISK_SLANG_SOURCE_DIR}/external/ieee1800"
        -MD -MF "${object}.d" -MQ "${object}"
        -c "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp" -o "${object}"
      DEPFILE "${object}.d"
      DEPENDS "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp"
        ${_obelisk_windows_runtime_headers}
      VERBATIM)
  endforeach()
  if(flavor STREQUAL "lto")
    set(archive "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}")
  else()
    set(archive "${OBELISK_TARGET_RUNTIME_ARCHIVE}")
  endif()
  add_custom_command(
    OUTPUT "${archive}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${archive}"
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-ar.exe" rcs "${archive}" ${objects}
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-ar.exe" qLs
      "${archive}" "${OBELISK_COMPILER_RT_BUILTINS}"
    DEPENDS ${objects} "${OBELISK_COMPILER_RT_BUILTINS}"
    VERBATIM)
endforeach()
add_custom_target(obelisk_target_runtime
  DEPENDS "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}")
set(_obelisk_windows_support_stamp "${CMAKE_BINARY_DIR}/windows-native-support.complete")
add_custom_command(
  OUTPUT "${_obelisk_windows_support_stamp}"
  BYPRODUCTS "${OBELISK_NATIVE_SUPPORT_DIR}/.complete"
    "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt.a"
    "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt_lto.a"
    "${OBELISK_NATIVE_SUPPORT_DIR}/obelisk-pi.def"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/LICENSE.TXT"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/CREDITS.TXT"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/obelisk/LICENSE"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${OBELISK_NATIVE_SUPPORT_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${OBELISK_TARGET_RUNTIME_ARCHIVE}" "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${OBELISK_NATIVE_SUPPORT_DIR}"
  COMMAND "${CMAKE_COMMAND}"
    "-DLLVM_NM=${OBELISK_LLVM_TOOLS_DIR}/llvm-nm.exe"
    "-DRUNTIME=${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "-DOUTPUT=${OBELISK_NATIVE_SUPPORT_DIR}/obelisk-pi.def"
    -P "${PROJECT_SOURCE_DIR}/cmake/StageWindowsPI.cmake"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/compiler-rt"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/obelisk"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/LICENSE.TXT"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/CREDITS.TXT"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/obelisk/LICENSE"
  COMMAND "${CMAKE_COMMAND}" -E touch "${OBELISK_NATIVE_SUPPORT_DIR}/.complete"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_obelisk_windows_support_stamp}"
  DEPENDS "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${PROJECT_SOURCE_DIR}/cmake/StageWindowsPI.cmake"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
    "${PROJECT_SOURCE_DIR}/LICENSE"
  VERBATIM)
add_custom_target(obelisk_native_support
  DEPENDS "${_obelisk_windows_support_stamp}")
add_dependencies(obelisk_native_support obelisk_target_runtime)
