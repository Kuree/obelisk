# GoogleTest is a test-only dependency. FetchContent adds the pinned source as
# a CMake subdirectory; it is never linked into the native runtime archive.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(googletest
  URL "https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz"
  URL_HASH
    "SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googletest)

add_library(TalosRuntimeCAPISmoke OBJECT
  RuntimeCAPISmoke.c
  VPI2023HeaderSmoke.c
)
target_link_libraries(TalosRuntimeCAPISmoke PRIVATE talos_rt)
target_include_directories(TalosRuntimeCAPISmoke SYSTEM BEFORE PRIVATE
  "${CMAKE_BINARY_DIR}/lib/talos/include"
)
set_target_properties(TalosRuntimeCAPISmoke PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

add_executable(TalosRuntimeTests
  ClassBitstreamTest.cpp
  ContainerTest.cpp
  CoverageBlockEventsTest.cpp
  CoverageDatabaseTest.cpp
  CoverageIndexTest.cpp
  CoverageTest.cpp
  DesignBytecodeTest.cpp
  DPIOpenArrayTest.cpp
  InitializationRandomTest.cpp
  RandSolveTest.cpp
  RandSolveWideTest.cpp
  RandModelCountTest.cpp
  ReadySetTest.cpp
  PromotionRangeIndexTest.cpp
  RuntimeControlTest.cpp
  RuntimeTest.cpp
  VCDTest.cpp
  VPIObjectModelTest.cpp
  VPIAutomaticStateTest.cpp
  $<TARGET_OBJECTS:TalosRuntimeCAPISmoke>
)
if(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  add_library(TalosVPICallbackTestModule MODULE
    Inputs/VPICallbackModule.c
    Inputs/VPICallbackThrow.cpp
  )
  target_include_directories(TalosVPICallbackTestModule SYSTEM PRIVATE
    ${TALOS_SLANG_SOURCE_DIR}/external/ieee1800
  )
  set_target_properties(TalosVPICallbackTestModule PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    PREFIX ""
  )
  add_dependencies(TalosRuntimeTests TalosVPICallbackTestModule)
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/VPICallbackTestConfig.h"
    CONTENT "#define TALOS_VPI_CALLBACK_TEST_MODULE \"$<TARGET_FILE:TalosVPICallbackTestModule>\"\n"
  )
  target_include_directories(TalosRuntimeTests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_target_properties(TalosRuntimeTests PROPERTIES ENABLE_EXPORTS ON)
endif()
target_link_libraries(TalosRuntimeTests PRIVATE
  talos_rt
  GTest::gtest_main
)
target_include_directories(TalosRuntimeTests SYSTEM PRIVATE
  ${TALOS_SLANG_SOURCE_DIR}/external/ieee1800
)
target_compile_features(TalosRuntimeTests PRIVATE cxx_std_17)
if(TALOS_ENABLE_Z3)
  # Existing residual-completion fixtures generate their plans with the
  # compiler backend inside the test process, then exercise the target sampler.
  target_compile_definitions(TalosRuntimeTests PRIVATE
    TALOS_TEST_COMPILED_SAMPLER=1)
  target_link_libraries(TalosRuntimeTests PRIVATE TalosSolver)
  target_include_directories(TalosRuntimeTests PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
endif()
if(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  target_compile_definitions(TalosRuntimeTests PRIVATE
    TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
set_target_properties(TalosRuntimeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(TalosProcessRuntimeTests
  DPIExportReentryTest.cpp
  ProcessTest.cpp
  TableProcessTest.cpp
)
target_link_libraries(TalosProcessRuntimeTests PRIVATE
  talos_rt
  GTest::gtest_main
)
target_compile_features(TalosProcessRuntimeTests PRIVATE cxx_std_17)
set_target_properties(TalosProcessRuntimeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# The lit suite covering these binaries is declared in test/Unit, so the
# regression suite's single lit invocation discovers it as a nested suite.
# check-talos-runtime-units still runs them on their own from there.
