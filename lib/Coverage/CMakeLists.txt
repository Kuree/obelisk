add_library(ObeliskCoverage STATIC
  CoverageDatabase.cpp
  CoverageFileSupport.cpp
)
add_dependencies(ObeliskCoverage
  ObeliskCoverageFormatDeclsIncGen
  ObeliskCoverageFormatParserIncGen
  ObeliskCoverageFormatSerializerIncGen)
target_include_directories(ObeliskCoverage PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
target_compile_features(ObeliskCoverage PUBLIC cxx_std_17)
target_link_libraries(ObeliskCoverage PRIVATE LLVMSupport)
set_target_properties(ObeliskCoverage PROPERTIES POSITION_INDEPENDENT_CODE ON)
