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
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's implicit parameter inference no longer matches the expected source")
  endif()

  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${expression_source}" "${contents}")
endif()

# IEEE 1800-2017 18.7 distinguishes a missing randomize identifier list from
# an explicitly empty one. Slang v11 stores both as an empty span, so preserve
# the syntactic presence separately while binding the inline constraint.
set(ast_context_header "${SOURCE_DIR}/include/slang/ast/ASTContext.h")
file(READ "${ast_context_header}" contents)

set(old_code [[
        /// A list of names to which class-scoped lookups are restricted.
        /// If empty, the lookup is unrestricted and all names are first
        /// tried in class-scope.
        std::span<const std::string_view> nameRestrictions;
]])
set(new_code [[
        /// A list of names to which class-scoped lookups are restricted.
        std::span<const std::string_view> nameRestrictions;

        /// Whether an identifier restriction list was explicitly provided.
        /// An explicit empty list restricts every unqualified name from
        /// beginning lookup in the randomized object's class scope.
        bool hasNameRestrictions = false;
]])

string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's randomize lookup context no longer matches the expected source")
  endif()

  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${ast_context_header}" "${contents}")
endif()

set(call_expression_source
  "${SOURCE_DIR}/source/ast/expressions/CallExpression.cpp")
file(READ "${call_expression_source}" contents)

set(old_code [[
            randomizeDetails.nameRestrictions = randInfo.constraintRestrictions;
            randInfo.inlineConstraints = &Constraint::bind(*withClause->constraints, argContext);
]])
set(new_code [[
            randomizeDetails.nameRestrictions = randInfo.constraintRestrictions;
            randomizeDetails.hasNameRestrictions = withClause->args != nullptr;
            randInfo.inlineConstraints = &Constraint::bind(*withClause->constraints, argContext);
]])

string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's randomize call binding no longer matches the expected source")
  endif()

  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${call_expression_source}" "${contents}")
endif()

set(lookup_source "${SOURCE_DIR}/source/ast/Lookup.cpp")
file(READ "${lookup_source}" contents)

set(old_code [[
            // If the nameRestrictions list is not empty, we have to verify that the
            // first element is in the list. Otherwise, an empty list indicates that
            // the lookup is unrestricted.
            if (!details.nameRestrictions.empty()) {
]])
set(new_code [[
            // If a restriction list was provided, verify that the first element is
            // in it. An explicit empty list prevents all class-scoped lookup.
            if (details.hasNameRestrictions) {
]])

string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's class randomize lookup no longer matches the expected source")
  endif()

  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${lookup_source}" "${contents}")
endif()

# IEEE 1800-2017 21.7.1.1 permits the optional $dumpfile argument to be a
# string or an integral expression interpreted as a character sequence. Slang
# v11 models it as a simple string-only task, which rejects the integral form
# before Obelisk can lower it.
set(system_tasks_source "${SOURCE_DIR}/source/ast/builtins/SystemTasks.cpp")
file(READ "${system_tasks_source}" contents)

set(old_code [[
class DumpVarsTask : public SystemTaskBase {
]])
set(new_code [[
class DumpFileTask : public SystemTaskBase {
public:
    DumpFileTask() : SystemTaskBase(KnownSystemName::DumpFile) {}

    const Type& checkArguments(const ASTContext& context, const Args& args, SourceRange range,
                               const Expression*) const final {
        auto& comp = context.getCompilation();
        if (!checkArgCount(context, false, args, range, 0, 1))
            return comp.getErrorType();
        if (!args.empty() && !args[0]->type->isString() && !args[0]->type->isIntegral())
            return badArg(context, *args[0]);
        return comp.getVoidType();
    }
};

class DumpVarsTask : public SystemTaskBase {
]])

string(FIND "${contents}" "class DumpFileTask : public SystemTaskBase" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's dumpfile task no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
    TASK(KnownSystemName::DumpFile, 0, &stringType);
]])
set(new_code [[
    addSystemSubroutine(std::make_shared<DumpFileTask>());
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's dumpfile registration no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

file(WRITE "${system_tasks_source}" "${contents}")
