find_package(Threads REQUIRED)

add_library(obelisk_rt STATIC
  lib/FileIO.cpp
  lib/Format.cpp
  lib/Runtime.cpp
)

add_library(ObeliskRuntime ALIAS obelisk_rt)

target_include_directories(obelisk_rt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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
