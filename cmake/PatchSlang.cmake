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

file(WRITE "${port_symbols_source}" "${contents}")

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

file(WRITE "${assignment_expressions_source}" "${contents}")

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

file(WRITE "${select_expressions_source}" "${contents}")
