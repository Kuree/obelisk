find_package(Threads REQUIRED)

add_library(obelisk_rt STATIC
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
  lib/DPI.cpp
  lib/FileIO.cpp
  lib/Format.cpp
  lib/ManagedHeap.cpp
  lib/Containers.cpp
  lib/Coverage.cpp
  lib/Process.cpp
  lib/ProcessAllocation.cpp
  lib/ProcessValidation.cpp
  lib/Random.cpp
  lib/Runtime.cpp
  lib/VPI.cpp
)

add_library(ObeliskRuntime ALIAS obelisk_rt)

target_include_directories(obelisk_rt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${OBELISK_SLANG_SOURCE_DIR}/external/ieee1800
)

target_compile_features(obelisk_rt PUBLIC cxx_std_17)
target_link_libraries(obelisk_rt PUBLIC Threads::Threads)
set_target_properties(obelisk_rt PROPERTIES
  OUTPUT_NAME obelisk_rt
  POSITION_INDEPENDENT_CODE ON
)

if(OBELISK_INCLUDE_TESTS)
  add_subdirectory(unittests)
endif()
