# Z3 in emitted simulations must use the target C++ ABI, which can differ
# from the compiler's ABI even when both execute on the same machine.
include(ExternalProject)

function(obelisk_build_runtime_z3 kind)
  set(_version "4.13.4")
  set(_source_args)
  set(_dependencies)
  if(OBELISK_Z3_SOURCE_DIR)
    set(_source "${OBELISK_Z3_SOURCE_DIR}")
    file(STRINGS "${_source}/CMakeLists.txt" _version_line
      REGEX "^project\\(Z3 VERSION ${_version}\\.0 LANGUAGES CXX\\)$")
    if(NOT _version_line)
      message(FATAL_ERROR "Runtime Z3 requires the pinned Z3 ${_version}")
    endif()
    list(APPEND _source_args SOURCE_DIR "${_source}" DOWNLOAD_COMMAND "")
  elseif(TARGET obelisk_z3_project)
    ExternalProject_Get_Property(obelisk_z3_project SOURCE_DIR)
    set(_source "${SOURCE_DIR}")
    list(APPEND _source_args SOURCE_DIR "${_source}" DOWNLOAD_COMMAND "")
    list(APPEND _dependencies obelisk_z3_project)
  else()
    set(_source "${CMAKE_BINARY_DIR}/runtime-z3-source")
    list(APPEND _source_args SOURCE_DIR "${_source}"
      URL "https://github.com/Z3Prover/z3/archive/refs/tags/z3-${_version}.tar.gz"
      URL_HASH "SHA256=4071977e66e9f3d239b7b098ceddfe62ffdf3c71e345e9524a4a5001d1f4adf3"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  set(_binary "${CMAKE_BINARY_DIR}/z3-${kind}-runtime")
  set(_path_flags
    "-ffile-prefix-map=\"${_source}\"=/obelisk/z3 -fmacro-prefix-map=\"${_source}\"=/obelisk/z3 -ffile-prefix-map=\"${_binary}\"=/obelisk/z3-build -fmacro-prefix-map=\"${_binary}\"=/obelisk/z3-build")
  set(_args
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    -DZ3_BUILD_LIBZ3_SHARED=OFF -DZ3_BUILD_EXECUTABLE=OFF
    -DZ3_BUILD_TEST_EXECUTABLES=OFF -DZ3_ENABLE_EXAMPLE_TARGETS=OFF
    -DZ3_BUILD_DOCUMENTATION=OFF -DZ3_BUILD_PYTHON_BINDINGS=OFF
    -DZ3_BUILD_DOTNET_BINDINGS=OFF -DZ3_BUILD_JAVA_BINDINGS=OFF
    -DZ3_BUILD_JULIA_BINDINGS=OFF -DZ3_INCLUDE_GIT_HASH=OFF
    -DZ3_INCLUDE_GIT_DESCRIBE=OFF -DZ3_LINK_TIME_OPTIMIZATION=OFF
    -DZ3_POLLING_TIMER=ON)
  if(kind STREQUAL "wasm")
    list(APPEND _args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_CXX_COMPILER=${_obelisk_wasm_cxx}"
      "-DCMAKE_CXX_FLAGS=-fexceptions -matomics -mbulk-memory -ffunction-sections -fdata-sections ${_path_flags}"
      -DZ3_SINGLE_THREADED=ON)
  else()
    list(APPEND _args
      "-DCMAKE_CXX_COMPILER=${OBELISK_LLVM_DIST_DIR}/bin/clang++"
      "-DCMAKE_CXX_COMPILER_TARGET=${OBELISK_TARGET_TRIPLE}"
      "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem \"${OBELISK_LLVM_DIST_DIR}/include/${OBELISK_TARGET_TRIPLE}/c++/v1\" -isystem \"${OBELISK_LLVM_DIST_DIR}/include/c++/v1\" -fPIC -ffunction-sections -fdata-sections ${_path_flags}"
      -DZ3_SINGLE_THREADED=OFF)
  endif()
  if(CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND _args
      "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
  endif()
  set(_target "obelisk_z3_${kind}_runtime")
  ExternalProject_Add(${_target}
    ${_source_args}
    BINARY_DIR "${_binary}"
    EXCLUDE_FROM_ALL TRUE
    DEPENDS ${_dependencies}
    CMAKE_ARGS ${_args}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${_binary}/libz3.a")
  set(OBELISK_TARGET_Z3_PROJECT "${_target}" PARENT_SCOPE)
  set(OBELISK_TARGET_Z3_ARCHIVE "${_binary}/libz3.a" PARENT_SCOPE)
  set(OBELISK_TARGET_Z3_INCLUDE_DIR "${_source}/src/api" PARENT_SCOPE)
endfunction()
