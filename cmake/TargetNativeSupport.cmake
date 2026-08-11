# Build-time provisioning for the only currently supported native target.

set(_obelisk_source_dir "${PROJECT_SOURCE_DIR}")
if(DEFINED OBELISK_SOURCE_DIR AND NOT OBELISK_SOURCE_DIR STREQUAL "")
  get_filename_component(_obelisk_source_dir "${OBELISK_SOURCE_DIR}" ABSOLUTE)
endif()
set(_obelisk_runtime_source_dir "${_obelisk_source_dir}/runtime")
if(DEFINED OBELISK_TARGET_RUNTIME_SOURCE_DIR AND
   NOT OBELISK_TARGET_RUNTIME_SOURCE_DIR STREQUAL "")
  get_filename_component(_obelisk_runtime_source_dir
    "${OBELISK_TARGET_RUNTIME_SOURCE_DIR}" ABSOLUTE)
endif()

set(OBELISK_TARGET_TRIPLE "x86_64-unknown-linux-gnu" CACHE STRING
    "Native code-generation target triple")
set(OBELISK_TARGET_SYSROOT_LAYOUT_VERSION "1" CACHE STRING
    "Internal target sysroot staging layout version")

set(_obelisk_libc6_name "libc6_2.28-10+deb10u4_amd64.deb")
set(_obelisk_libc6_hash
    "80b59743f7b47f0644d211d56918851404e66ab7fbba17e60e90664c12fc5822")
set(_obelisk_libc6_dev_name "libc6-dev_2.28-10+deb10u4_amd64.deb")
set(_obelisk_libc6_dev_hash
    "d759a8102b932dc51e3a25b8cc3b91f3718adda774b0c42b431117662b4750cc")
set(_obelisk_linux_libc_name "linux-libc-dev_4.19.316-1_amd64.deb")
set(_obelisk_linux_libc_hash
    "fde95d52b753e7b9b372d22b5c7154b31bea3b9d63239ef0c40655da94c141a3")

set(OBELISK_TARGET_LIBC6_URL
    "https://archive.debian.org/debian-security/pool/updates/main/g/glibc/${_obelisk_libc6_name}"
    CACHE STRING "Pinned libc6 package URL (https:// or file://)")
set(OBELISK_TARGET_LIBC6_DEV_URL
    "https://archive.debian.org/debian-security/pool/updates/main/g/glibc/${_obelisk_libc6_dev_name}"
    CACHE STRING "Pinned libc6-dev package URL (https:// or file://)")
set(OBELISK_TARGET_LINUX_LIBC_DEV_URL
    "https://archive.debian.org/debian-security/pool/updates/main/l/linux/${_obelisk_linux_libc_name}"
    CACHE STRING "Pinned linux-libc-dev package URL (https:// or file://)")

set(_obelisk_cache_default "${CMAKE_BINARY_DIR}/_target-package-cache")
if(DEFINED ENV{OBELISK_TARGET_PACKAGE_CACHE} AND
   NOT "$ENV{OBELISK_TARGET_PACKAGE_CACHE}" STREQUAL "")
  set(_obelisk_cache_default "$ENV{OBELISK_TARGET_PACKAGE_CACHE}")
endif()
set(OBELISK_TARGET_PACKAGE_CACHE "${_obelisk_cache_default}" CACHE PATH
    "Persistent cache for verified target .deb archives")

set(_obelisk_sysroot_override_default "")
if(DEFINED ENV{OBELISK_TARGET_SYSROOT_DIR} AND
   NOT "$ENV{OBELISK_TARGET_SYSROOT_DIR}" STREQUAL "")
  set(_obelisk_sysroot_override_default "$ENV{OBELISK_TARGET_SYSROOT_DIR}")
endif()
set(OBELISK_TARGET_SYSROOT_DIR "${_obelisk_sysroot_override_default}" CACHE PATH
    "Pre-extracted target sysroot; validated and used without copying")

set(_obelisk_key_material
    "layout=${OBELISK_TARGET_SYSROOT_LAYOUT_VERSION};target=${OBELISK_TARGET_TRIPLE};${_obelisk_libc6_name}=${_obelisk_libc6_hash};${_obelisk_libc6_dev_name}=${_obelisk_libc6_dev_hash};${_obelisk_linux_libc_name}=${_obelisk_linux_libc_hash}")
if(OBELISK_TARGET_SYSROOT_DIR)
  get_filename_component(_obelisk_override_root
    "${OBELISK_TARGET_SYSROOT_DIR}" ABSOLUTE)
  file(GLOB_RECURSE _obelisk_override_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES TRUE RELATIVE "${_obelisk_override_root}"
    "${_obelisk_override_root}/*")
  list(SORT _obelisk_override_files)
  set(_obelisk_override_material "")
  set(_obelisk_override_dependencies)
  foreach(relative IN LISTS _obelisk_override_files)
    set(path "${_obelisk_override_root}/${relative}")
    if(IS_DIRECTORY "${path}" AND NOT IS_SYMLINK "${path}")
      continue()
    endif()
    if(IS_SYMLINK "${path}")
      file(READ_SYMLINK "${path}" target)
      string(APPEND _obelisk_override_material
        "symlink:${relative}=${target};")
      # An absolute link that is valid relative to the sysroot is normally
      # dangling from the host's perspective. Ninja treats such a path as a
      # missing input, so watch its containing directory instead. Replacing a
      # link changes the directory timestamp and therefore causes CMake to
      # recompute the content-addressed key.
      get_filename_component(parent "${path}" DIRECTORY)
      list(APPEND _obelisk_override_dependencies "${parent}")
    else()
      list(APPEND _obelisk_override_dependencies "${path}")
      file(SHA256 "${path}" digest)
      string(APPEND _obelisk_override_material
        "file:${relative}=${digest};")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _obelisk_override_dependencies)
  # File contents, not just additions/removals discovered by the glob, are
  # configure inputs because they participate in the content-addressed key.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_obelisk_override_dependencies})
  string(SHA256 _obelisk_override_fingerprint
    "${_obelisk_override_material}")
  string(APPEND _obelisk_key_material
    ";preextracted=${_obelisk_override_fingerprint}")
endif()
string(SHA256 OBELISK_TARGET_SYSROOT_KEY "${_obelisk_key_material}")
set(_obelisk_target_root
    "${CMAKE_BINARY_DIR}/target-sysroots/${OBELISK_TARGET_SYSROOT_KEY}")
set(OBELISK_TARGET_SYSROOT "${_obelisk_target_root}/sysroot")
if(OBELISK_TARGET_SYSROOT_DIR)
  get_filename_component(OBELISK_TARGET_SYSROOT
    "${OBELISK_TARGET_SYSROOT_DIR}" ABSOLUTE)
endif()
set(OBELISK_TARGET_SYSROOT_STAMP "${_obelisk_target_root}/.complete")

add_custom_command(
  OUTPUT "${OBELISK_TARGET_SYSROOT_STAMP}"
  COMMAND "${CMAKE_COMMAND}"
    "-DSTAMP=${OBELISK_TARGET_SYSROOT_STAMP}"
    "-DSYSROOT=${OBELISK_TARGET_SYSROOT}"
    "-DPREEXTRACTED_SYSROOT=${OBELISK_TARGET_SYSROOT_DIR}"
    "-DTARGET_TRIPLE=${OBELISK_TARGET_TRIPLE}"
    "-DLAYOUT_VERSION=${OBELISK_TARGET_SYSROOT_LAYOUT_VERSION}"
    "-DPACKAGE_CACHE=${OBELISK_TARGET_PACKAGE_CACHE}"
    "-DPACKAGE0_NAME=${_obelisk_libc6_name}"
    "-DPACKAGE0_URL=${OBELISK_TARGET_LIBC6_URL}"
    "-DPACKAGE0_SHA256=${_obelisk_libc6_hash}"
    "-DPACKAGE1_NAME=${_obelisk_libc6_dev_name}"
    "-DPACKAGE1_URL=${OBELISK_TARGET_LIBC6_DEV_URL}"
    "-DPACKAGE1_SHA256=${_obelisk_libc6_dev_hash}"
    "-DPACKAGE2_NAME=${_obelisk_linux_libc_name}"
    "-DPACKAGE2_URL=${OBELISK_TARGET_LINUX_LIBC_DEV_URL}"
    "-DPACKAGE2_SHA256=${_obelisk_linux_libc_hash}"
    -P "${_obelisk_source_dir}/cmake/ProvisionTargetSysroot.cmake"
  DEPENDS "${_obelisk_source_dir}/cmake/ProvisionTargetSysroot.cmake"
          ${_obelisk_override_dependencies}
  COMMENT "Provisioning pinned glibc 2.28 target sysroot"
  VERBATIM)
add_custom_target(obelisk_target_sysroot
  DEPENDS "${OBELISK_TARGET_SYSROOT_STAMP}")

# The build-graph regression test includes this file in a small standalone
# project so it can exercise reconfiguration and no-op behavior without
# rebuilding the compiler itself.
if(OBELISK_TARGET_SYSROOT_ONLY)
  return()
endif()

foreach(tool clang++ llvm-ar llvm-ranlib)
  if(NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/bin/${tool}")
    message(FATAL_ERROR
      "The pinned LLVM distribution is missing target-runtime tool: "
      "${OBELISK_LLVM_DIST_DIR}/bin/${tool}")
  endif()
endforeach()

set(_obelisk_target_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(OBELISK_TARGET_RUNTIME_ARCHIVE
    "${_obelisk_target_runtime_dir}/libobelisk_rt.a")
set(OBELISK_TARGET_RUNTIME_LTO_ARCHIVE
    "${_obelisk_target_runtime_dir}/libobelisk_rt_lto.a")
file(GLOB_RECURSE _obelisk_target_runtime_headers CONFIGURE_DEPENDS
  "${_obelisk_runtime_source_dir}/include/*.h"
  "${_obelisk_runtime_source_dir}/lib/*.h")
set(_obelisk_target_runtime_objects)
set(_obelisk_target_runtime_lto_objects)
set(_obelisk_target_runtime_definitions)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _obelisk_target_runtime_definitions
    -DOBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
foreach(source ABI Bytecode Containers Coverage DesignBytecode
               DesignBytecodeImage DesignBytecodeIntrinsics
               DesignBytecodeLogic DesignBytecodeNets DesignBytecodeObservers
               DesignBytecodeRoots DesignDatabase DPI
               FileIO Format ManagedHeap Plusargs Process
               ProcessAllocation ProcessAOT ProcessNativeState ProcessNBA
               ProcessObservers ProcessSignals
               ProcessState ProcessTransitions ProcessValidation Random RandSolve RandSolveWide
               Runtime Sampled VCD VPI)
  set(object "${_obelisk_target_runtime_dir}/${source}.o")
  set(lto_object "${_obelisk_target_runtime_dir}/${source}.bc")
  list(APPEND _obelisk_target_runtime_objects "${object}")
  list(APPEND _obelisk_target_runtime_lto_objects "${lto_object}")
  add_custom_command(
    OUTPUT "${object}" "${lto_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_obelisk_target_runtime_dir}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/clang++"
      --target=${OBELISK_TARGET_TRIPLE}
      --sysroot=${OBELISK_TARGET_SYSROOT}
      -std=c++17 -O3 -fPIC -fvisibility=hidden
      ${_obelisk_target_runtime_definitions}
      -ffunction-sections -fdata-sections
      "-ffile-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      "-fmacro-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      -nostdinc++
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/${OBELISK_TARGET_TRIPLE}/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/lib/clang/22/include"
      -I "${_obelisk_runtime_source_dir}/include"
      -I "${_obelisk_runtime_source_dir}/lib"
      -c "${_obelisk_runtime_source_dir}/lib/${source}.cpp" -o "${object}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/clang++"
      --target=${OBELISK_TARGET_TRIPLE}
      --sysroot=${OBELISK_TARGET_SYSROOT}
      -std=c++17 -O3 -flto=full -funified-lto -fPIC -fvisibility=hidden
      ${_obelisk_target_runtime_definitions}
      -ffunction-sections -fdata-sections
      "-ffile-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      "-fmacro-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      -nostdinc++
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/${OBELISK_TARGET_TRIPLE}/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/lib/clang/22/include"
      -I "${_obelisk_runtime_source_dir}/include"
      -I "${_obelisk_runtime_source_dir}/lib"
      -c "${_obelisk_runtime_source_dir}/lib/${source}.cpp" -o "${lto_object}"
    DEPENDS
      "${OBELISK_TARGET_SYSROOT_STAMP}"
      "${_obelisk_runtime_source_dir}/lib/${source}.cpp"
      ${_obelisk_target_runtime_headers}
    COMMENT "Building native and Full-LTO target runtime ${source}.cpp"
    VERBATIM)
endforeach()
add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
          ${_obelisk_target_runtime_objects}
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  DEPENDS ${_obelisk_target_runtime_objects}
  COMMENT "Archiving target libobelisk_rt.a with llvm-ar"
  VERBATIM)
add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
          ${_obelisk_target_runtime_lto_objects}
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  DEPENDS ${_obelisk_target_runtime_lto_objects}
  COMMENT "Archiving target libobelisk_rt_lto.a with llvm-ar"
  VERBATIM)
add_custom_target(obelisk_target_runtime
  DEPENDS
    "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}")
add_dependencies(obelisk_target_runtime obelisk_target_sysroot)

set(OBELISK_NATIVE_SUPPORT_DIR
    "${CMAKE_BINARY_DIR}/lib/obelisk/targets/${OBELISK_TARGET_TRIPLE}")
set(OBELISK_NATIVE_SUPPORT_STAMP
    "${CMAKE_BINARY_DIR}/native-support-${OBELISK_TARGET_SYSROOT_KEY}-llvm-${OBELISK_LLVM_VERSION}.complete")
set(_obelisk_native_support_byproducts
  "${OBELISK_NATIVE_SUPPORT_DIR}/.complete"
  "${OBELISK_NATIVE_SUPPORT_DIR}/README.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/BUILD_PATH_PREFIXES.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/clang_rt.crtbegin.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/clang_rt.crtend.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libclang_rt.builtins.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libc++.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libc++abi.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libunwind.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt_lto.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/Scrt1.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/crti.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/crtn.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/libc.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/libc_nonshared.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/libm.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/libmvec_nonshared.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/libpthread.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/libdl.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/usr/lib/x86_64-linux-gnu/librt.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libc.so.6"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libc-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libm.so.6"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libm-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libmvec.so.1"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libmvec-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libpthread.so.0"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libpthread-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libdl.so.2"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/libdl-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/librt.so.1"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/librt-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/ld-2.28.so"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
  "${OBELISK_NATIVE_SUPPORT_DIR}/glibc/lib64/ld-linux-x86-64.so.2"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/glibc/libc6-copyright"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/glibc/libc6-dev-copyright"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/glibc/linux-libc-dev-copyright"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/glibc/LGPL-2.1.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/glibc/GPL-2.0.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/llvm/LICENSE.TXT"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/llvm/Apache-2.0.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/llvm/LLVM-exception.txt")
add_custom_command(
  OUTPUT "${OBELISK_NATIVE_SUPPORT_STAMP}"
  BYPRODUCTS ${_obelisk_native_support_byproducts}
  COMMAND "${CMAKE_COMMAND}"
    "-DSTAMP=${OBELISK_NATIVE_SUPPORT_STAMP}"
    "-DDESTINATION=${OBELISK_NATIVE_SUPPORT_DIR}"
    "-DSYSROOT=${OBELISK_TARGET_SYSROOT}"
    "-DRUNTIME_ARCHIVE=${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "-DRUNTIME_LTO_ARCHIVE=${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "-DLLVM_DIST=${OBELISK_LLVM_DIST_DIR}"
    "-DSOURCE_DIR=${_obelisk_source_dir}"
    "-DSTAGE_KEY=${OBELISK_TARGET_SYSROOT_KEY}-llvm-${OBELISK_LLVM_VERSION}"
    "-DTARGET_TRIPLE=${OBELISK_TARGET_TRIPLE}"
    -P "${_obelisk_source_dir}/cmake/StageNativeSupport.cmake"
  DEPENDS
    "${OBELISK_TARGET_SYSROOT_STAMP}"
    "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${_obelisk_source_dir}/cmake/StageNativeSupport.cmake"
    "${_obelisk_source_dir}/docs/third-party/licenses/LGPL-2.1.txt"
    "${_obelisk_source_dir}/docs/third-party/licenses/GPL-2.0.txt"
    "${_obelisk_source_dir}/docs/third-party/licenses/Apache-2.0.txt"
    "${_obelisk_source_dir}/docs/third-party/licenses/LLVM-exception.txt"
  COMMENT "Staging local Obelisk native-link support"
  VERBATIM)
add_custom_target(obelisk_native_support
  DEPENDS "${OBELISK_NATIVE_SUPPORT_STAMP}")
add_dependencies(obelisk_native_support obelisk_target_runtime)

message(STATUS "Native target sysroot key: ${OBELISK_TARGET_SYSROOT_KEY}")
message(STATUS "Native target sysroot: ${OBELISK_TARGET_SYSROOT}")
