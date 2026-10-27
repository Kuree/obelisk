add_library(TalosCoverage STATIC
  CoverageDatabase.cpp
  CoverageFileSupport.cpp
)
add_dependencies(TalosCoverage
  TalosCoverageFormatDeclsIncGen
  TalosCoverageFormatParserIncGen
  TalosCoverageFormatSerializerIncGen)
target_include_directories(TalosCoverage PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
target_compile_features(TalosCoverage PUBLIC cxx_std_17)
target_link_libraries(TalosCoverage PRIVATE LLVMSupport)
set_target_properties(TalosCoverage PROPERTIES POSITION_INDEPENDENT_CODE ON)
