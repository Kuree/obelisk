if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(expression_source "${SOURCE_DIR}/source/ast/Expression.cpp")
file(READ "${expression_source}" contents)

set(old_code [[
            if (expr.isUnsizedInteger())
                bits = std::max(bits, 32u);
]])
set(new_code [[
            if (expr.isUnsizedInteger() &&
                expr.kind != ExpressionKind::UnbasedUnsizedIntegerLiteral)
                bits = std::max(bits, 32u);
]])

string(FIND "${contents}" "${new_code}" patched_at)
if(NOT patched_at EQUAL -1)
  return()
endif()

string(FIND "${contents}" "${old_code}" unpatched_at)
if(unpatched_at EQUAL -1)
  message(FATAL_ERROR
    "Slang's implicit parameter inference no longer matches the expected source")
endif()

string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
file(WRITE "${expression_source}" "${contents}")
