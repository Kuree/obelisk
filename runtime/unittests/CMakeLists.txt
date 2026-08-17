# GoogleTest is a test-only dependency. FetchContent adds the pinned source as
# a CMake subdirectory; it is never linked into the native runtime archive.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
  URL "https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz"
  URL_HASH
    "SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googletest)

add_library(ObeliskRuntimeCAPISmoke OBJECT
  RuntimeCAPISmoke.c
)
target_link_libraries(ObeliskRuntimeCAPISmoke PRIVATE obelisk_rt)
set_target_properties(ObeliskRuntimeCAPISmoke PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

add_executable(ObeliskRuntimeTests
  ContainerTest.cpp
  CoverageTest.cpp
  DesignBytecodeTest.cpp
  RandSolveTest.cpp
  RandSolveWideTest.cpp
  RuntimeControlTest.cpp
  RuntimeTest.cpp
  VCDTest.cpp
  $<TARGET_OBJECTS:ObeliskRuntimeCAPISmoke>
)
target_link_libraries(ObeliskRuntimeTests PRIVATE
  obelisk_rt
  GTest::gtest_main
)
target_include_directories(ObeliskRuntimeTests SYSTEM PRIVATE
  ${OBELISK_SLANG_SOURCE_DIR}/external/ieee1800
)
target_compile_features(ObeliskRuntimeTests PRIVATE cxx_std_17)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  target_compile_definitions(ObeliskRuntimeTests PRIVATE
    OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
set_target_properties(ObeliskRuntimeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(ObeliskProcessRuntimeTests
  ProcessTest.cpp
)
target_link_libraries(ObeliskProcessRuntimeTests PRIVATE
  obelisk_rt
  GTest::gtest_main
)
target_compile_features(ObeliskProcessRuntimeTests PRIVATE cxx_std_17)
set_target_properties(ObeliskProcessRuntimeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# The lit suite covering these binaries is declared in test/Unit, so the
# regression suite's single lit invocation discovers it as a nested suite.
# check-obelisk-runtime-units still runs them on their own from there.
