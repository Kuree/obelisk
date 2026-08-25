if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

# FetchContent reruns this patch command when the parent project reconfigures.
# Avoid changing source mtimes when all requested patches are already present;
# otherwise an unrelated test glob or CMake edit recompiles patched slang TUs.
function(obelisk_write_if_different path new_contents)
  file(READ "${path}" old_contents)
  if(NOT old_contents STREQUAL new_contents)
    file(WRITE "${path}" "${new_contents}")
  endif()
endfunction()

# Slang's version probe walks upward looking for `.git` before it invokes Git,
# so an archive extracted below Obelisk's repository incorrectly adopts the
# Obelisk commit. Bound that filesystem search at the fetched release root. The
# sentinel has no HEAD, which is the same metadata state as the release archive
# extracted outside a repository: v11.0 reports patch 0 and hash 0. Explicit
# source overrides do not request this and retain their own repository metadata.
if(RELEASE_ARCHIVE)
  file(MAKE_DIRECTORY "${SOURCE_DIR}/.git")
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

# IEEE 1800-2017 20.4.2 gives each $timeformat argument a default and permits
# an empty argument to select it. Slang v11 marks the arguments optional by
# count but still rejects an explicit empty position before lowering sees it.
set(old_code [[
class DisplayTask : public SystemTaskBase {
]])
set(new_code [[
class TimeFormatTask : public SimpleSystemTask {
public:
    TimeFormatTask(const Type& voidType, const Type& intType, const Type& stringType) :
        SimpleSystemTask(KnownSystemName::TimeFormat, voidType, 0,
                         {&intType, &intType, &stringType, &intType}) {}

    bool allowEmptyArgument(size_t) const final { return true; }
};

class DisplayTask : public SystemTaskBase {
]])
string(FIND "${contents}" "class TimeFormatTask : public SimpleSystemTask" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's display task location no longer matches the expected source")
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

set(old_code [[
    TASK(KnownSystemName::TimeFormat, 0, &intType, &intType, &stringType, &intType);
]])
set(new_code [[
    addSystemSubroutine(std::make_shared<TimeFormatTask>(voidType, intType, stringType));
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's timeformat registration no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

obelisk_write_if_different("${system_tasks_source}" "${contents}")

# IEEE 1800-2017 6.6.7 permits a package-qualified user-defined nettype in an
# ANSI or non-ANSI port declaration. Slang v11 only probes an unqualified
# simple type name while deciding whether a catch-all port header denotes a
# variable or a net, so a qualified nettype is misclassified as a variable.
set(port_symbols_source
  "${SOURCE_DIR}/source/ast/symbols/PortSymbols.cpp")
file(READ "${port_symbols_source}" contents)

set(old_code [[
namespace {

const NetType& getDefaultNetType(const Scope& scope, SourceLocation location) {
]])
set(new_code [[
namespace {

const NetType* lookupPortNetType(const Scope& scope, const DataTypeSyntax& syntax) {
    if (syntax.kind == SyntaxKind::NamedType) {
        const auto& named = syntax.as<NamedTypeSyntax>();
        ASTContext context(scope, LookupLocation::max, ASTFlags::AllowNetType);
        LookupResult result;
        Lookup::name(*named.name, context, LookupFlags::Type, result);
        if (result.found && result.found->kind == SymbolKind::NetType)
            return &result.found->as<NetType>();
    }

    std::string_view simpleName = SyntaxFacts::getSimpleTypeName(syntax);
    if (simpleName.empty())
        return nullptr;
    const Symbol* found = Lookup::unqualified(scope, simpleName, LookupFlags::Type);
    return found && found->kind == SymbolKind::NetType ? &found->as<NetType>() : nullptr;
}

const NetType& getDefaultNetType(const Scope& scope, SourceLocation location) {
]])
string(FIND "${contents}" "const NetType* lookupPortNetType" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's port nettype lookup helper location no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
                std::string_view simpleName = SyntaxFacts::getSimpleTypeName(*header.dataType);
                if (!simpleName.empty()) {
                    auto found = Lookup::unqualified(scope, simpleName, LookupFlags::Type);
                    if (found && found->kind == SymbolKind::NetType) {
                        return add(decl, getDirection(header.direction), nullptr,
                                   &found->as<NetType>(), syntax.attributes);
                    }

                    // If we didn't find a valid type, try to find a definition.
]])
set(new_code [[
                std::string_view simpleName = SyntaxFacts::getSimpleTypeName(*header.dataType);
                if (!simpleName.empty() || header.dataType->kind == SyntaxKind::NamedType) {
                    auto netType = lookupPortNetType(scope, *header.dataType);
                    if (netType) {
                        return add(decl, getDirection(header.direction), nullptr,
                                   netType, syntax.attributes);
                    }

                    auto found = simpleName.empty()
                                     ? nullptr
                                     : Lookup::unqualified(scope, simpleName,
                                                           LookupFlags::Type);

                    // If we didn't find a valid type, try to find a definition.
]])
string(FIND "${contents}" "auto netType = lookupPortNetType(scope, *header.dataType);" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's ANSI port nettype lookup no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
                        auto typeName = SyntaxFacts::getSimpleTypeName(*varHeader.dataType);
                        auto result = Lookup::unqualified(scope, typeName, LookupFlags::Type);
                        if (result && result->kind == SymbolKind::NetType) {
                            auto net = comp.emplace<NetSymbol>(name, declLoc,
                                                               result->as<NetType>());
]])
set(new_code [[
                        auto result = lookupPortNetType(scope, *varHeader.dataType);
                        if (result) {
                            auto net = comp.emplace<NetSymbol>(name, declLoc,
                                                               *result);
]])
string(FIND "${contents}" "auto result = lookupPortNetType(scope, *varHeader.dataType);" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's non-ANSI port nettype lookup no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

obelisk_write_if_different("${port_symbols_source}" "${contents}")

# IEEE 1800-2017 10.9 defines an assignment-pattern type key as a
# simple_type. A ps_type_identifier is syntactically a simple_type even when
# its canonical target is an enum, struct, or another non-simple Type object.
# Slang v11 instead asks Type::isSimpleType(), rejecting legal typedef and
# package-qualified keys before Obelisk can import them.
set(assignment_expressions_source
  "${SOURCE_DIR}/source/ast/expressions/AssignmentExpressions.cpp")
file(READ "${assignment_expressions_source}" contents)

set(old_code [[
static void bindDefaultSetter(const ASTContext& context, const AssignmentPatternItemSyntax& item,
]])
set(new_code [[
static bool isAssignmentPatternTypeKey(const Type& type,
                                       const ExpressionSyntax& syntax) {
    // A ps_type_identifier is syntactically a simple_type regardless of the
    // canonical type it names. In particular, an enum or aggregate typedef
    // remains a legal assignment-pattern type key.
    return type.isSimpleType() || NameSyntax::isKind(syntax.kind);
}

static void bindDefaultSetter(const ASTContext& context, const AssignmentPatternItemSyntax& item,
]])
string(FIND "${contents}" "static bool isAssignmentPatternTypeKey" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's assignment-pattern helper location no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
        else if (DataTypeSyntax::isKind(item->key->kind)) {
            const Type& typeKey = comp.getType(item->key->as<DataTypeSyntax>(), context);
            if (typeKey.isSimpleType()) {
                auto& expr = bindRValue(typeKey, *item->expr, {}, context);
                typeSetters.emplace_back(TypeSetter{&typeKey, &expr});
                bad |= expr.bad();
            }
            else {
                context.addDiag(diag::AssignmentPatternKeyExpr, item->key->sourceRange());
                bad = true;
            }
        }
        else {
            context.addDiag(diag::AssignmentPatternKeyExpr, item->key->sourceRange());
            bad = true;
        }
]])
set(new_code [[
        else {
            auto& keyExpr = Expression::bind(*item->key, context, ASTFlags::AllowDataType);
            if (!keyExpr.bad() && keyExpr.kind == ExpressionKind::DataType &&
                isAssignmentPatternTypeKey(*keyExpr.type, *item->key)) {
                const Type& typeKey = *keyExpr.type;
                auto& expr = bindRValue(typeKey, *item->expr, {}, context);
                typeSetters.emplace_back(TypeSetter{&typeKey, &expr});
                bad |= expr.bad();
            }
            else if (!keyExpr.bad()) {
                context.addDiag(diag::AssignmentPatternKeyExpr, item->key->sourceRange());
                bad = true;
            }
            else {
                bad = true;
            }
        }
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's struct assignment-pattern type-key binding no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
            if (typeKey.isSimpleType()) {
                auto& expr = bindRValue(typeKey, *item->expr, {}, context);
                typeSetters.emplace_back(TypeSetter{&typeKey, &expr});
]])
set(new_code [[
            if (isAssignmentPatternTypeKey(typeKey, *item->key)) {
                auto& expr = bindRValue(typeKey, *item->expr, {}, context);
                typeSetters.emplace_back(TypeSetter{&typeKey, &expr});
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's array assignment-pattern type-key binding no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

# Slang v11 initially binds an untyped assignment pattern used as a default
# setter against its error type because it ordinarily lacks assignment
# context. Array patterns do have one unambiguous element type, so binding the
# nested pattern to that type avoids retaining an InvalidExpression in an
# otherwise valid elaborated AST and implements recursive array defaults.
set(old_code [[
static void bindDefaultSetter(const ASTContext& context, const AssignmentPatternItemSyntax& item,
                              const Expression*& defaultSetter, bool& bad) {
]])
set(new_code [[
static void bindDefaultSetter(const ASTContext& context, const AssignmentPatternItemSyntax& item,
                              const Expression*& defaultSetter, bool& bad,
                              const Type* nestedTargetType = nullptr) {
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's default assignment-pattern setter signature no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
    if (expr->kind == SyntaxKind::AssignmentPatternExpression &&
        !expr->as<AssignmentPatternExpressionSyntax>().type) {
        defaultSetter = &Expression::bindRValue(context.getCompilation().getErrorType(), *item.expr,
                                                {}, context);
    }
]])
set(new_code [[
    if (expr->kind == SyntaxKind::AssignmentPatternExpression &&
        !expr->as<AssignmentPatternExpressionSyntax>().type) {
        if (nestedTargetType) {
            defaultSetter = &Expression::bindRValue(*nestedTargetType, *item.expr, {}, context);
            bad |= defaultSetter->bad();
        }
        else {
            defaultSetter = &Expression::bindRValue(context.getCompilation().getErrorType(),
                                                    *item.expr, {}, context);
        }
    }
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's nested default assignment-pattern binding no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
    SmallVector<TypeSetter, 4> typeSetters;

    for (auto item : syntax.items) {
        if (item->key->kind == SyntaxKind::DefaultPatternKeyExpression) {
            bindDefaultSetter(context, *item, defaultSetter, bad);
            continue;
]])
set(new_code [[
    SmallVector<TypeSetter, 4> typeSetters;

    for (auto item : syntax.items) {
        if (item->key->kind == SyntaxKind::DefaultPatternKeyExpression) {
            bindDefaultSetter(context, *item, defaultSetter, bad, &elementType);
            continue;
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's fixed-array default setter call no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
    size_t maxIndex = 0;

    for (auto item : syntax.items) {
        if (item->key->kind == SyntaxKind::DefaultPatternKeyExpression) {
            bindDefaultSetter(context, *item, defaultSetter, bad);
            continue;
]])
set(new_code [[
    size_t maxIndex = 0;

    for (auto item : syntax.items) {
        if (item->key->kind == SyntaxKind::DefaultPatternKeyExpression) {
            bindDefaultSetter(context, *item, defaultSetter, bad, &elementType);
            continue;
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's dynamic-array default setter call no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
    const Type* indexType = type.getAssociativeIndexType();

    for (auto item : syntax.items) {
        if (item->key->kind == SyntaxKind::DefaultPatternKeyExpression) {
            bindDefaultSetter(context, *item, defaultSetter, bad);
]])
set(new_code [[
    const Type* indexType = type.getAssociativeIndexType();

    for (auto item : syntax.items) {
        if (item->key->kind == SyntaxKind::DefaultPatternKeyExpression) {
            bindDefaultSetter(context, *item, defaultSetter, bad, &elementType);
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's associative-array default setter call no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

obelisk_write_if_different("${assignment_expressions_source}" "${contents}")

# Equality operands provide assignment context to each other under the usual
# aggregate comparison rules. Slang v11 binds both operands independently,
# so an untyped assignment pattern is rejected before the other operand can
# supply its type. Bind the ordinary side first and pass that type to the
# pattern binder, symmetrically for patterns on either side.
set(operator_expressions_source
  "${SOURCE_DIR}/source/ast/expressions/OperatorExpressions.cpp")
file(READ "${operator_expressions_source}" contents)

set(old_code [[
        flags |= ASTFlags::AllowTypeReferences;

        // Special case to handle comparing a virtual interface with an
        // actual instance. We can't normally bind to an instance from
        // an expression so we need to explicitly try that separately here.
        lhs = tryBindInterfaceRef(context, syntaxLeft, /* isInterfacePort */ false);
        if (!lhs)
            lhs = &create(compilation, syntaxLeft, context, flags);

        // If we found a virtual interface on the lhs we can also try for an instance
        // on the rhs. Otherwise we know we're doing normal expression binding.
        if (lhs->type->isVirtualInterface()) {
            rhs = tryBindInterfaceRef(context, syntaxRight, /* isInterfacePort */ false);
            if (!rhs) {
                rhs = &create(compilation, syntaxRight, context, flags);
            }
            else if (lhs->kind == ExpressionKind::ArbitrarySymbol &&
                     rhs->kind == ExpressionKind::ArbitrarySymbol) {
                // Having an instance on both sides is not allowed. One side must be
                // an actual virtual interface.
                context.addDiag(diag::CannotCompareTwoInstances, syntax.operatorToken.location())
                    << lhs->sourceRange << rhs->sourceRange;
                return badExpr(compilation, nullptr);
            }
        }
        else {
            rhs = &create(compilation, syntaxRight, context, flags);
        }
]])
set(new_code [[
        flags |= ASTFlags::AllowTypeReferences;

        auto isUntypedAssignmentPattern = [](const ExpressionSyntax& expr) {
            if (expr.kind != SyntaxKind::AssignmentPatternExpression)
                return false;
            return !expr.as<AssignmentPatternExpressionSyntax>().type;
        };
        bool lhsPattern = isUntypedAssignmentPattern(syntaxLeft);
        bool rhsPattern = isUntypedAssignmentPattern(syntaxRight);

        // An untyped assignment pattern takes its comparison operand's type
        // as assignment context. Bind the ordinary operand first so the
        // pattern can be elaborated without a spurious no-context diagnostic.
        if (lhsPattern != rhsPattern) {
            if (lhsPattern) {
                rhs = &create(compilation, syntaxRight, context, flags);
                lhs = &create(compilation, syntaxLeft, context, flags, rhs->type);
            }
            else {
                lhs = &create(compilation, syntaxLeft, context, flags);
                rhs = &create(compilation, syntaxRight, context, flags, lhs->type);
            }
        }

        // Special case to handle comparing a virtual interface with an
        // actual instance. We can't normally bind to an instance from
        // an expression so we need to explicitly try that separately here.
        else {
            lhs = tryBindInterfaceRef(context, syntaxLeft, /* isInterfacePort */ false);
            if (!lhs)
                lhs = &create(compilation, syntaxLeft, context, flags);

            // If we found a virtual interface on the lhs we can also try for an instance
            // on the rhs. Otherwise we know we're doing normal expression binding.
            if (lhs->type->isVirtualInterface()) {
                rhs = tryBindInterfaceRef(context, syntaxRight, /* isInterfacePort */ false);
                if (!rhs) {
                    rhs = &create(compilation, syntaxRight, context, flags);
                }
                else if (lhs->kind == ExpressionKind::ArbitrarySymbol &&
                         rhs->kind == ExpressionKind::ArbitrarySymbol) {
                    // Having an instance on both sides is not allowed. One side must be
                    // an actual virtual interface.
                    context.addDiag(diag::CannotCompareTwoInstances,
                                    syntax.operatorToken.location())
                        << lhs->sourceRange << rhs->sourceRange;
                    return badExpr(compilation, nullptr);
                }
            }
            else {
                rhs = &create(compilation, syntaxRight, context, flags);
            }
        }
]])
string(FIND "${contents}" "isUntypedAssignmentPattern" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's equality operand binding no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${operator_expressions_source}" "${contents}")
endif()

# A conditional whose two arms are null still receives its surrounding
# assignment target. Keep that handle type on the conditional itself so
# lowering can materialize a typed null instead of an unusable bare NullType.
set(old_code [[
        if (lt->isNull() && rt->isNull()) {
            result->type = &comp.getNullType();
]])
set(new_code [[
        if (lt->isNull() && rt->isNull()) {
            result->type = assignmentTarget && assignmentTarget->isHandleType() &&
                                   !assignmentTarget->isNull()
                               ? assignmentTarget
                               : &comp.getNullType();
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's null conditional result typing no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${operator_expressions_source}" "${contents}")
endif()

# Slang v11 lets a queue lvalue select name the append slot one past its
# current end, but accidentally applies that allowance to rvalue selects too.
# Reading q[0] from an empty queue during speculative constant evaluation then
# reaches deque::at(0) and terminates the compiler. This is the upstream fix:
# make the append allowance explicit and enable it only for lvalue evaluation.
set(select_expressions_header
  "${SOURCE_DIR}/include/slang/ast/expressions/SelectExpressions.h")
file(READ "${select_expressions_header}" contents)

set(old_code [[
    std::optional<ConstantRange> evalIndex(EvalContext& context, const ConstantValue& val,
                                           ConstantValue& associativeIndex, bool& softFail) const;
]])
set(new_code [[
    std::optional<ConstantRange> evalIndex(EvalContext& context, const ConstantValue& val,
                                           ConstantValue& associativeIndex, bool& softFail,
                                           bool allowQueueAppend = false) const;
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's element-select index declaration no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${select_expressions_header}" "${contents}")
endif()

set(select_expressions_source
  "${SOURCE_DIR}/source/ast/expressions/SelectExpressions.cpp")
file(READ "${select_expressions_source}" contents)

set(old_code [[
    auto range = evalIndex(context, loadedVal, associativeIndex, softFail);
]])
set(new_code [[
    auto range = evalIndex(context, loadedVal, associativeIndex, softFail,
                           /*allowQueueAppend=*/true);
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's lvalue element-select evaluation no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
std::optional<ConstantRange> ElementSelectExpression::evalIndex(EvalContext& context,
                                                                const ConstantValue& val,
                                                                ConstantValue& associativeIndex,
                                                                bool& softFail) const {
]])
set(new_code [[
std::optional<ConstantRange> ElementSelectExpression::evalIndex(EvalContext& context,
                                                                const ConstantValue& val,
                                                                ConstantValue& associativeIndex,
                                                                bool& softFail,
                                                                bool allowQueueAppend) const {
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's element-select index definition no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
        size_t maxIndex = val.size();
        if (val.isQueue())
            maxIndex++;
]])
set(new_code [[
        // A write may target the append slot one past the end of a queue; a
        // read at that index is out of bounds and returns the element default.
        size_t maxIndex = val.size();
        if (val.isQueue() && allowQueueAppend)
            maxIndex++;
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's dynamic element-select bounds check no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

obelisk_write_if_different("${select_expressions_source}" "${contents}")

# IEEE 1800-2017 8.24 permits an out-of-block method definition whose class
# scope is itself nested, for example C::Nested::method. Slang v11 parses only
# one class qualifier and keys definitions by that unqualified class name.
# Accept an arbitrary identifier-only class scope and key it canonically so
# same-named nested classes remain distinct.
set(parser_members_source
  "${SOURCE_DIR}/source/parsing/Parser_members.cpp")
file(READ "${parser_members_source}" contents)
set(old_code [[
static bool checkSubroutineName(const NameSyntax& name) {
    auto checkKind = [](auto& node) {
        return node.kind == SyntaxKind::IdentifierName || node.kind == SyntaxKind::ConstructorName;
    };

    if (name.kind == SyntaxKind::ScopedName) {
        auto& scoped = name.as<ScopedNameSyntax>();
        return checkKind(*scoped.left) && checkKind(*scoped.right);
    }

    return checkKind(name);
}
]])
set(new_code [[
static bool checkSubroutineName(const NameSyntax& name) {
    auto checkFinalKind = [](const NameSyntax& node) {
        return node.kind == SyntaxKind::IdentifierName ||
               node.kind == SyntaxKind::ConstructorName;
    };

    auto checkClassScope = [&](const auto& self, const NameSyntax& node) -> bool {
        if (node.kind == SyntaxKind::IdentifierName)
            return true;
        if (node.kind != SyntaxKind::ScopedName)
            return false;

        auto& scoped = node.as<ScopedNameSyntax>();
        return scoped.separator.kind == TokenKind::DoubleColon && self(self, *scoped.left) &&
               scoped.right->kind == SyntaxKind::IdentifierName;
    };

    if (name.kind == SyntaxKind::ScopedName) {
        auto& scoped = name.as<ScopedNameSyntax>();
        if (scoped.separator.kind == TokenKind::Dot)
            return checkFinalKind(*scoped.left) && checkFinalKind(*scoped.right);
        return scoped.separator.kind == TokenKind::DoubleColon &&
               checkClassScope(checkClassScope, *scoped.left) && checkFinalKind(*scoped.right);
    }

    return checkFinalKind(name);
}
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's out-of-block subroutine-name check no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${parser_members_source}" "${contents}")
endif()

set(compilation_header "${SOURCE_DIR}/include/slang/ast/Compilation.h")
file(READ "${compilation_header}" contents)
set(old_code [[
    mutable flat_hash_map<
        std::tuple<std::string_view, std::string_view, const Scope*>,
        std::tuple<const syntax::SyntaxNode*, const syntax::ScopedNameSyntax*, SymbolIndex, bool>>
        outOfBlockDecls;
]])
set(new_code [[
    mutable flat_hash_map<
        std::tuple<std::string, std::string_view, const Scope*>,
        std::tuple<const syntax::SyntaxNode*, const syntax::ScopedNameSyntax*, SymbolIndex, bool>>
        outOfBlockDecls;
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's out-of-block declaration map no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${compilation_header}" "${contents}")
endif()

set(compilation_source "${SOURCE_DIR}/source/ast/Compilation.cpp")
file(READ "${compilation_source}" contents)
set(old_code [[
void Compilation::addOutOfBlockDecl(const Scope& scope, const ScopedNameSyntax& name,
                                    const SyntaxNode& syntax, SymbolIndex index) {
    SLANG_ASSERT(!isFrozen());

    std::string_view className = name.left->getLastToken().valueText();
    std::string_view declName = name.right->getLastToken().valueText();
    auto [it, inserted] = outOfBlockDecls.emplace(std::make_tuple(className, declName, &scope),
                                                  std::make_tuple(&syntax, &name, index, false));

    if (!inserted && !className.empty() && !declName.empty()) {
        std::string combined = fmt::format("{}::{}", className, declName);
        auto range = std::get<1>(it->second)->sourceRange();

        auto& diag = scope.addDiag(diag::Redefinition, name.sourceRange());
        diag << combined;
        diag.addNote(diag::NotePreviousDefinition, range);
    }
}

std::tuple<const SyntaxNode*, SymbolIndex, bool*> Compilation::findOutOfBlockDecl(
    const Scope& scope, std::string_view className, std::string_view declName) const {

    auto it = outOfBlockDecls.find({className, declName, &scope});
    if (it != outOfBlockDecls.end()) {
        auto& [syntax, name, index, used] = it->second;
        return {syntax, index, &used};
    }

    return {nullptr, SymbolIndex(), nullptr};
}
]])
set(new_code [[
static void appendClassScopeName(const NameSyntax& name, std::string& result) {
    if (name.kind == SyntaxKind::ScopedName) {
        auto& scoped = name.as<ScopedNameSyntax>();
        appendClassScopeName(*scoped.left, result);
        result += "::";
        appendClassScopeName(*scoped.right, result);
        return;
    }

    result += name.getLastToken().valueText();
}

void Compilation::addOutOfBlockDecl(const Scope& scope, const ScopedNameSyntax& name,
                                    const SyntaxNode& syntax, SymbolIndex index) {
    SLANG_ASSERT(!isFrozen());

    std::string className;
    appendClassScopeName(*name.left, className);
    std::string_view declName = name.right->getLastToken().valueText();
    auto [it, inserted] = outOfBlockDecls.emplace(std::make_tuple(className, declName, &scope),
                                                  std::make_tuple(&syntax, &name, index, false));

    if (!inserted && !className.empty() && !declName.empty()) {
        std::string combined = fmt::format("{}::{}", className, declName);
        auto range = std::get<1>(it->second)->sourceRange();

        auto& diag = scope.addDiag(diag::Redefinition, name.sourceRange());
        diag << combined;
        diag.addNote(diag::NotePreviousDefinition, range);
    }
}

std::tuple<const SyntaxNode*, SymbolIndex, bool*> Compilation::findOutOfBlockDecl(
    const Scope& scope, std::string_view className, std::string_view declName) const {

    std::string classPath(className);
    const Scope* declarationScope = &scope;
    while (declarationScope->asSymbol().kind == SymbolKind::ClassType) {
        classPath.insert(0, "::");
        classPath.insert(0, declarationScope->asSymbol().name);
        declarationScope = declarationScope->asSymbol().getParentScope();
        SLANG_ASSERT(declarationScope);
    }

    auto it = outOfBlockDecls.find({classPath, declName, declarationScope});
    if (it != outOfBlockDecls.end()) {
        auto& [syntax, name, index, used] = it->second;
        return {syntax, index, &used};
    }

    return {nullptr, SymbolIndex(), nullptr};
}
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's out-of-block declaration lookup no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()

set(old_code [[
                auto classRange = name->left->sourceRange();
                auto sym = Lookup::unqualifiedAt(*scope, className,
                                                 LookupLocation(scope, uint32_t(index)),
                                                 classRange);
]])
set(new_code [[
                auto classRange = name->left->sourceRange();
                auto firstClassName = name->left->getFirstToken().valueText();
                auto sym = Lookup::unqualifiedAt(*scope, firstClassName,
                                                 LookupLocation(scope, uint32_t(index)),
                                                 classRange);
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's unused out-of-block diagnostic no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
endif()
obelisk_write_if_different("${compilation_source}" "${contents}")

set(subroutine_symbols_source
  "${SOURCE_DIR}/source/ast/symbols/SubroutineSymbols.cpp")
file(READ "${subroutine_symbols_source}" contents)
set(old_code [[
    // The method definition must be located after the class definition.
    if (index <= parentSym.getIndex()) {
        auto& diag = outerScope.addDiag(diag::MemberDefinitionBeforeClass,
                                        syntax->prototype->name->getLastToken().location());
        diag << name << parentSym.name;
        diag.addNote(diag::NoteDeclarationHere, parentSym.location);
    }
]])
set(new_code [[
    // The method definition must be located after the outermost containing
    // class definition. Nested class member indexes are local to their class
    // scope and cannot be compared to the definition's enclosing-scope index.
    const Symbol* sourceOrderedClass = &parentSym;
    for (const Scope* parentScope = parentSym.getParentScope();
         parentScope && parentScope->asSymbol().kind == SymbolKind::ClassType;
         parentScope = parentScope->asSymbol().getParentScope()) {
        sourceOrderedClass = &parentScope->asSymbol();
    }
    if (index <= sourceOrderedClass->getIndex()) {
        auto& diag = outerScope.addDiag(diag::MemberDefinitionBeforeClass,
                                        syntax->prototype->name->getLastToken().location());
        diag << name << parentSym.name;
        diag.addNote(diag::NoteDeclarationHere, parentSym.location);
    }
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's out-of-block method source-order check no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${subroutine_symbols_source}" "${contents}")
endif()

set(class_symbols_source "${SOURCE_DIR}/source/ast/symbols/ClassSymbols.cpp")
file(READ "${class_symbols_source}" contents)
set(old_code [[
        // The method definition must be located after the class definition.
        outOfBlockIndex = index;
        if (index <= parentSym.getIndex()) {
            auto& diag = outerScope.addDiag(diag::MemberDefinitionBeforeClass,
                                            cds.name->getLastToken().location());
            diag << name << parentSym.name;
            diag.addNote(diag::NoteDeclarationHere, parentSym.location);
        }
]])
set(new_code [[
        // Compare source order against the outermost containing class; nested
        // member indexes belong to a different scope than this definition.
        outOfBlockIndex = index;
        const Symbol* sourceOrderedClass = &parentSym;
        for (const Scope* parentScope = parentSym.getParentScope();
             parentScope && parentScope->asSymbol().kind == SymbolKind::ClassType;
             parentScope = parentScope->asSymbol().getParentScope()) {
            sourceOrderedClass = &parentScope->asSymbol();
        }
        if (index <= sourceOrderedClass->getIndex()) {
            auto& diag = outerScope.addDiag(diag::MemberDefinitionBeforeClass,
                                            cds.name->getLastToken().location());
            diag << name << parentSym.name;
            diag.addNote(diag::NoteDeclarationHere, parentSym.location);
        }
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's out-of-block constraint source-order check no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${class_symbols_source}" "${contents}")
endif()

# Annex D.2 permits a scalar net or a bit-select of a vector net. Slang v11
# asks getSymbolReference to reject every packed select before checking the
# selected expression's width, diagnosing the specified vector form.
set(non_const_funcs_source
  "${SOURCE_DIR}/source/ast/builtins/NonConstFuncs.cpp")
file(READ "${non_const_funcs_source}" contents)
set(old_code [[
        auto sym = args[0]->getSymbolReference(/* allowPacked */ false);
        if (!sym || sym->kind != SymbolKind::Net)
            context.addDiag(diag::ExpectedNetRef, args[0]->sourceRange);
]])
set(new_code [[
        auto sym = args[0]->getSymbolReference(/* allowPacked */ true);
        if (!sym || sym->kind != SymbolKind::Net || args[0]->type->getBitWidth() != 1)
            context.addDiag(diag::ExpectedNetRef, args[0]->sourceRange);
]])
string(FIND "${contents}" "${new_code}" patched_at)
if(patched_at EQUAL -1)
  string(FIND "${contents}" "${old_code}" unpatched_at)
  if(unpatched_at EQUAL -1)
    message(FATAL_ERROR
      "Slang's countdrivers net-reference check no longer matches the expected source")
  endif()
  string(REPLACE "${old_code}" "${new_code}" contents "${contents}")
  file(WRITE "${non_const_funcs_source}" "${contents}")
endif()
