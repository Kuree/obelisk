# Verify that the selected slang release's AST dispatch surface matches Obelisk's
# checked-in exhaustive inventory. This deliberately parses ASTVisitor.h rather
# than a documentation list: adding, removing, or renaming an upstream concrete
# dispatch case must stop the build.

if(NOT DEFINED SLANG_AST_VISITOR OR NOT EXISTS "${SLANG_AST_VISITOR}")
  message(FATAL_ERROR "SLANG_AST_VISITOR does not name ASTVisitor.h")
endif()
if(NOT DEFINED OBELISK_AST_INVENTORY OR
   NOT EXISTS "${OBELISK_AST_INVENTORY}")
  message(FATAL_ERROR "OBELISK_AST_INVENTORY does not name SlangASTNodes.def")
endif()

file(READ "${SLANG_AST_VISITOR}" visitor)
file(READ "${OBELISK_AST_INVENTORY}" inventory)

# Invalid symbol, statement, and expression sentinels are diagnosed before
# emission and have explicit importer overloads that fail. They are not
# semantic nodes.
string(REGEX MATCHALL
  "(SYMBOL|TYPE)\\([A-Za-z][A-Za-z0-9_]*\\)|CASE\\([A-Za-z][A-Za-z0-9_]*, [A-Za-z][A-Za-z0-9_]*\\)"
  visitor_cases "${visitor}")
list(FILTER visitor_cases EXCLUDE REGEX
  "^(SYMBOL|TYPE)\\(k\\)|^CASE\\(k, n\\)|^CASE\\(Invalid, Invalid(Statement|Expression)\\)$")

# Some dispatches are written out explicitly rather than through SYMBOL/TYPE
# macros. Derive their category from the concrete C++ class name so additions
# to this part of ASTVisitor also fail the inventory check.
string(REGEX MATCHALL
  "case SymbolKind::[A-Za-z][A-Za-z0-9_]*: return visitor.visit\\(\\*static_cast<const [A-Za-z][A-Za-z0-9_]*\\*>"
  explicit_symbol_cases "${visitor}")
foreach(entry IN LISTS explicit_symbol_cases)
  string(REGEX REPLACE
    ".*SymbolKind::([A-Za-z][A-Za-z0-9_]*):.*const ([A-Za-z][A-Za-z0-9_]*)\\*>.*"
    "\\1;\\2" dispatch "${entry}")
  list(GET dispatch 0 kind)
  list(GET dispatch 1 cpp_type)
  if(kind STREQUAL "k")
    continue()
  endif()
  if(cpp_type MATCHES "Symbol$")
    list(APPEND visitor_cases "SYMBOL(${kind})")
  else()
    list(APPEND visitor_cases "TYPE(${kind})")
  endif()
endforeach()
list(SORT visitor_cases)

string(REGEX MATCHALL
  "SLANG_AST_NODE\\([^,]+, [^,]+, [^)]+\\)"
  inventory_entries "${inventory}")
set(inventory_cases)
foreach(entry IN LISTS inventory_entries)
  if(entry MATCHES
     "^SLANG_AST_NODE\\(([^,]+), ([^,]+), ([^)]+)\\)$")
    set(category "${CMAKE_MATCH_1}")
    set(kind "${CMAKE_MATCH_2}")
    set(cpp_type "${CMAKE_MATCH_3}")
    if(category STREQUAL "Symbol")
      list(APPEND inventory_cases "SYMBOL(${kind})")
    elseif(category STREQUAL "Type")
      list(APPEND inventory_cases "TYPE(${kind})")
    elseif(category MATCHES
           "^(Statement|Timing|Expression|Constraint|Assertion|Bins|Pattern|RandSeq)$")
      list(APPEND inventory_cases "CASE(${kind}, ${cpp_type})")
    endif()
  endif()
endforeach()
list(SORT inventory_cases)

list(LENGTH visitor_cases visitor_count)
list(LENGTH inventory_cases inventory_count)
if(NOT visitor_count EQUAL 220)
  message(FATAL_ERROR
    "Selected slang AST inventory changed: expected 220 semantic cases but "
    "ASTVisitor.h contains ${visitor_count}. Update the Slang dialect, "
    "importer overloads, Obelisk operations, conversion patterns, and tests.")
endif()
if(NOT inventory_count EQUAL 220)
  message(FATAL_ERROR
    "Obelisk SlangASTNodes.def must contain 220 entries; found "
    "${inventory_count}")
endif()
if(NOT visitor_cases STREQUAL inventory_cases)
  message(FATAL_ERROR
    "Slang AST dispatch and Obelisk inventory differ.\n"
    "slang:   ${visitor_cases}\n"
    "obelisk: ${inventory_cases}")
endif()
