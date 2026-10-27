# Reuse completed dependency artifacts without configuring or writing into the
# supplying build. Slang's public definitions affect its header ABI, so read
# those settings from the original Talos cache rather than the consuming build.
function(talos_import_prebuilt_slang)
  set(build "${TALOS_SLANG_PREBUILT_BUILD_DIR}")
  if(NOT EXISTS "${build}/CMakeCache.txt")
    message(FATAL_ERROR
      "TALOS_SLANG_PREBUILT_BUILD_DIR must name an existing Talos CMake "
      "build directory, not its Slang dependency subdirectory")
  endif()
  load_cache("${build}" READ_WITH_PREFIX prebuilt_
    CMAKE_PROJECT_NAME CMAKE_CXX_COMPILER CMAKE_BUILD_TYPE
    CMAKE_CONFIGURATION_TYPES CMAKE_TOOLCHAIN_FILE
    BUILD_SHARED_LIBS Boost_DIR slang_SOURCE_DIR slang_BINARY_DIR
    SLANG_LIB_NAME SLANG_USE_THREADS SLANG_USE_MIMALLOC SLANG_USE_CPPTRACE
    SLANG_INCLUDE_PYLIB FMT_SOURCE_DIR FMT_DEBUG_POSTFIX FMT_MODULE
    FMT_SAFE_DURATION_CAST)
  if(NOT prebuilt_CMAKE_PROJECT_NAME STREQUAL "talos" OR
     prebuilt_CMAKE_CONFIGURATION_TYPES OR prebuilt_BUILD_SHARED_LIBS OR
     prebuilt_SLANG_INCLUDE_PYLIB OR prebuilt_SLANG_USE_MIMALLOC OR
     prebuilt_SLANG_USE_CPPTRACE OR prebuilt_Boost_DIR OR prebuilt_FMT_MODULE)
    message(FATAL_ERROR
      "TALOS_SLANG_PREBUILT_BUILD_DIR requires a single-configuration "
      "Talos build with static Slang, vendored Boost, and no mimalloc, "
      "cpptrace, Python library, or fmt modules")
  endif()
  if(CMAKE_CROSSCOMPILING OR prebuilt_CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
      "Reusing prebuilt Slang currently requires native builds without "
      "a dependency toolchain file")
  endif()
  file(REAL_PATH "${CMAKE_CXX_COMPILER}" compiler)
  file(REAL_PATH "${prebuilt_CMAKE_CXX_COMPILER}" prebuilt_compiler)
  if(NOT compiler STREQUAL prebuilt_compiler)
    message(FATAL_ERROR "Prebuilt Slang must use the same C++ compiler")
  endif()

  set(source "${prebuilt_slang_SOURCE_DIR}")
  set(binary "${prebuilt_slang_BINARY_DIR}")
  set(fmt_library "${binary}/lib/libfmt.a")
  if(prebuilt_CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(fmt_library "${binary}/lib/libfmt${prebuilt_FMT_DEBUG_POSTFIX}.a")
  endif()
  set(slang_library "${binary}/lib/lib${prebuilt_SLANG_LIB_NAME}.a")
  foreach(artifact IN ITEMS
      "${source}/CMakeLists.txt"
      "${source}/include/slang/ast/Compilation.h"
      "${source}/external/boost_unordered.hpp"
      "${binary}/source/slang/slang_export.h"
      "${binary}/source/slang/syntax/AllSyntax.h"
      "${binary}/source/slang/diagnostics/AllDiags.h"
      "${prebuilt_FMT_SOURCE_DIR}/include/fmt/format.h"
      "${slang_library}" "${fmt_library}")
    if(NOT EXISTS "${artifact}")
      message(FATAL_ERROR "Prebuilt Slang is missing ${artifact}")
    endif()
  endforeach()
  file(STRINGS "${source}/CMakeLists.txt" version
    REGEX "^set\\(SLANG_VERSION_(MAJOR|MINOR) [0-9]+\\)$")
  string(REPLACE "." ";" expected_version "${TALOS_SLANG_VERSION}")
  list(GET expected_version 0 major)
  list(GET expected_version 1 minor)
  if(NOT version STREQUAL
     "set(SLANG_VERSION_MAJOR ${major});set(SLANG_VERSION_MINOR ${minor})")
    message(FATAL_ERROR "Prebuilt Slang must be v${TALOS_SLANG_VERSION}")
  endif()

  add_library(fmt::fmt STATIC IMPORTED GLOBAL)
  set_target_properties(fmt::fmt PROPERTIES
    IMPORTED_LOCATION "${fmt_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${prebuilt_FMT_SOURCE_DIR}/include")
  if(prebuilt_FMT_SAFE_DURATION_CAST)
    target_compile_definitions(fmt::fmt INTERFACE FMT_SAFE_DURATION_CAST)
  endif()
  # Keep the upstream target name: the MLIR frontend's object library uses its
  # presence to propagate the vendored Boost header ABI explicitly.
  add_library(boost_unordered INTERFACE IMPORTED GLOBAL)
  target_compile_definitions(boost_unordered INTERFACE SLANG_BOOST_SINGLE_HEADER)
  add_library(Boost::headers ALIAS boost_unordered)
  add_library(slang::slang STATIC IMPORTED GLOBAL)
  set_target_properties(slang::slang PROPERTIES
    IMPORTED_LOCATION "${slang_library}"
    INTERFACE_INCLUDE_DIRECTORIES
      "${source}/include;${binary}/source;${source}/external")
  target_compile_definitions(slang::slang INTERFACE SLANG_STATIC_DEFINE)
  target_link_libraries(slang::slang INTERFACE fmt::fmt Boost::headers)
  if(prebuilt_CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(slang::slang INTERFACE SLANG_DEBUG)
  endif()
  if(prebuilt_SLANG_USE_THREADS)
    find_package(Threads REQUIRED)
    target_compile_definitions(slang::slang INTERFACE SLANG_USE_THREADS)
    target_link_libraries(slang::slang INTERFACE Threads::Threads)
  endif()
  set(TALOS_SLANG_SOURCE_DIR "${source}" PARENT_SCOPE)
  message(STATUS
    "Reusing slang v${TALOS_SLANG_VERSION} from Talos build ${build}")
endfunction()
