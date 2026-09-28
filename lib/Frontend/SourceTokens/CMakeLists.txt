# The slang lexer behind `obelisk -dump-tokens`, usable without the rest of
# the frontend: its header is C++17 and exposes no slang or MLIR types.
add_library(ObeliskSourceTokens STATIC
  SourceTokens.cpp
)
target_include_directories(ObeliskSourceTokens PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_compile_features(ObeliskSourceTokens PUBLIC cxx_std_17 PRIVATE cxx_std_20)
target_link_libraries(ObeliskSourceTokens PUBLIC LLVMSupport PRIVATE slang::slang)
if(TARGET boost_unordered)
  target_compile_definitions(ObeliskSourceTokens
    PRIVATE SLANG_BOOST_SINGLE_HEADER)
endif()
set_target_properties(ObeliskSourceTokens PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
