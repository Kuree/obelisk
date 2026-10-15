//===- PrepareDeclarations.cpp - Executable declaration planning ---------===//
//
// Freezes covergroup and class metadata into deterministic executable
// declarations before individual code units are isolated.
//
//===----------------------------------------------------------------------===//

#include "PrepareDeclarations.h"

#include "Detail.h"
#include "PrepareRandomFunctions.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

bool isWeakReferenceClass(semantic::SVClassTypeOp classType) {
  return getHierarchyName(classType).starts_with("std::weak_reference#(");
}

/// Whether a normalized source can reach an object handle while recursively
/// forming its bit stream. This is intentionally structural and stops at the
/// class handle: the class declaration graph is symbol-based rather than a
/// recursively embedded MLIR type.
bool containsClassBitstreamSource(Type type) {
  SmallVector<Type, 16> pending{type};
  llvm::SmallPtrSet<const void *, 16> visited;
  while (!pending.empty()) {
    Type current = pending.pop_back_val();
    if (!visited.insert(current.getAsOpaquePointer()).second)
      continue;
    if (isa<sim::ClassHandleType>(current))
      return true;
    if (auto array = dyn_cast<sim::DynamicArrayType>(current)) {
      pending.push_back(array.getElementType());
      continue;
    }
    if (auto queue = dyn_cast<sim::QueueType>(current)) {
      pending.push_back(queue.getElementType());
      continue;
    }
    if (auto associative = dyn_cast<sim::AssocArrayType>(current)) {
      pending.push_back(associative.getElementType());
      continue;
    }
    if (auto array = dyn_cast<sim::UnpackedArrayType>(current)) {
      pending.push_back(array.getElementType());
      continue;
    }
    if (!isa<sim::UnpackedStructType>(current))
      continue;
    for (unsigned ordinal = 0, count = sim::getAggregateNumElements(current);
         ordinal != count; ++ordinal)
      pending.push_back(sim::getAggregateElementType(current, ordinal));
  }
  return false;
}

/// The local/protected-property exception in 6.24.3 applies only to the exact
/// current-instance expression.  Resolve that identity while the semantic
/// symbol reference is still available; lowered SSA equality would also bless
/// aliases of `this` and is therefore too permissive.
bool isExactCurrentInstanceThis(Operation *expression) {
  while (auto conversion =
             dyn_cast<semantic::SVConversionExpressionOp>(expression)) {
    BoolAttr implicit = conversion->getAttrOfType<BoolAttr>("is_implicit");
    if (implicit && !implicit.getValue())
      return false;
    SmallVector<Operation *> children = getChildren(conversion);
    if (children.size() != 1)
      return false;
    expression = children.front();
  }
  auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression);
  if (!named)
    return false;
  SymbolRefAttr referenced = named.getReferencedSymbolAttr();
  if (!referenced)
    return false;
  for (Operation *parent = expression->getParentOp(); parent;
       parent = parent->getParentOp())
    if (auto current =
            parent->getAttrOfType<SymbolRefAttr>("this_variable_symbol"))
      return current == referenced;
  return false;
}

uint64_t getVirtualMethodSignatureID(semantic::SVSubroutineSymbolOp method) {
  std::string key;
  llvm::raw_string_ostream stream(key);
  if (auto name = method->getAttrOfType<StringAttr>("name"))
    stream << name.getValue();
  stream << '#' << static_cast<uint32_t>(method.getSubroutineKind()) << '#';
  if (auto type = method->getAttrOfType<TypeAttr>("semantic_type"))
    type.getValue().print(stream);
  return stableCodeUnitID(key);
}

StringRef getSourceSymbolName(StringRef symbol) {
  size_t separator = symbol.find('.');
  return separator == StringRef::npos ? symbol
                                      : symbol.drop_front(separator + 1);
}

bool isBuiltinBaseClass(semantic::ClassHandleType handle) {
  SymbolRefAttr name = handle.getClassName();
  if (name.getNestedReferences().size() != 1 ||
      getSourceSymbolName(name.getRootReference().getValue()) != "std")
    return false;
  StringRef leaf = getSourceSymbolName(name.getLeafReference().getValue());
  return leaf == "semaphore" || leaf == "mailbox";
}

bool isBuiltinBaseMethodPath(StringRef path) {
  return path.starts_with("std::semaphore::") ||
         path.starts_with("std::mailbox#(");
}

/// The nested symbol reference that names `operation` from the module.
SymbolRefAttr getSemanticSymbolPath(Operation *operation) {
  SmallVector<StringAttr> names;
  for (Operation *current = operation; current && !isa<ModuleOp>(current);
       current = current->getParentOp())
    if (auto symbol = current->getAttrOfType<StringAttr>(
            SymbolTable::getSymbolAttrName()))
      names.push_back(symbol);
  std::reverse(names.begin(), names.end());
  SmallVector<FlatSymbolRefAttr> nested;
  for (StringAttr name : ArrayRef(names).drop_front())
    nested.push_back(FlatSymbolRefAttr::get(name));
  return SymbolRefAttr::get(names.front(), nested);
}

/// Creates semantic operations with fresh node IDs.
class SemanticOpFactory {
public:
  explicit SemanticOpFactory(ModuleOp module) : builder(module.getContext()) {
    module.walk([&](Operation *operation) {
      if (auto node = operation->getAttrOfType<IntegerAttr>("node_id"))
        nextNode = std::max(nextNode, node.getInt() + 1);
    });
  }

  Operation *create(OpBuilder &at, Location location, StringRef name,
                    ArrayRef<NamedAttribute> attributes) {
    OperationState state(location, name);
    state.addAttributes(attributes);
    state.addAttribute("node_id", builder.getI64IntegerAttr(nextNode++));
    state.addRegion();
    Operation *operation = at.create(state);
    operation->getRegion(0).push_back(new Block);
    return operation;
  }

  Builder builder;

private:
  int64_t nextNode = 0;
};

} // namespace

LogicalResult normalizeDefaultedConstructors(ModuleOp module) {
  SmallVector<semantic::SVSubroutineSymbolOp> constructors;
  module.walk([&](semantic::SVSubroutineSymbolOp subroutine) {
    if (subroutine.getIsConstructor().value_or(false) &&
        subroutine->hasAttr("has_defaulted_super_arg"))
      constructors.push_back(subroutine);
  });
  if (constructors.empty())
    return success();
  llvm::StringMap<semantic::SVClassTypeOp> classes;
  module.walk([&](semantic::SVClassTypeOp classType) {
    classes[classType.getSymName()] = classType;
  });
  SemanticOpFactory factory(module);
  Builder &builder = factory.builder;
  MLIRContext *context = module.getContext();
  for (semantic::SVSubroutineSymbolOp constructor : constructors) {
    auto owner = constructor->getParentOfType<semantic::SVClassTypeOp>();
    std::optional<Type> baseType =
        owner ? owner.getBaseClass() : std::optional<Type>{};
    auto baseHandle =
        baseType ? dyn_cast<semantic::ClassHandleType>(*baseType) : nullptr;
    semantic::SVClassTypeOp base =
        baseHandle
            ? classes.lookup(baseHandle.getClassName().getLeafReference())
            : semantic::SVClassTypeOp{};
    if (!base)
      return constructor.emitError(
          "constructor with a default argument has no base class");
    // The base constructor whose argument list `default` expands.
    semantic::SVSubroutineSymbolOp baseConstructor;
    if (auto symbol = base->getAttrOfType<SymbolRefAttr>("constructor_symbol"))
      for (Operation *member : getChildren(base))
        if (auto method = dyn_cast<semantic::SVSubroutineSymbolOp>(member);
            method &&
            method.getSymName() == symbol.getLeafReference().getValue())
          baseConstructor = method;

    // An implicit constructor of `extends Base(default)` executes like a
    // written one: it takes the expanded arguments and forwards them.
    if (constructor.getIsBuiltin().value_or(false)) {
      constructor->removeAttr("is_builtin");
      if (!constructor.getThisVariablePath())
        constructor->setAttr(
            "this_variable_path",
            builder.getStringAttr(getHierarchyName(constructor) + ".this"));
    }

    // IEEE 1800-2023 8.17: super.new(default) passes every argument that
    // `default` expanded, and a constructor with `default` and no super.new
    // gets an implicit super.new(default).
    semantic::SVNewClassExpressionOp superCall;
    constructor->walk([&](semantic::SVNewClassExpressionOp construct) {
      if (construct.getIsSuperClass() && !superCall &&
          construct->getParentOfType<semantic::SVSubroutineSymbolOp>() ==
              constructor)
        superCall = construct;
    });
    if (superCall && !getChildren(superCall).empty())
      continue;
    if (!baseConstructor) {
      // The base takes no arguments; implicit chaining constructs it.
      if (superCall) {
        Operation *statement = superCall->getParentOp();
        if (isa<semantic::SVExpressionStatementOp>(statement))
          statement->erase();
        else
          superCall->erase();
      }
      continue;
    }
    llvm::StringMap<semantic::SVFormalArgumentSymbolOp> formals;
    for (Operation *member : getChildren(constructor))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(member))
        if (auto name = formal->getAttrOfType<StringAttr>("name"))
          formals[name.getValue()] = formal;
    SmallVector<semantic::SVFormalArgumentSymbolOp> forwarded;
    for (Operation *member : getChildren(baseConstructor)) {
      auto baseFormal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(member);
      if (!baseFormal)
        continue;
      auto name = baseFormal->getAttrOfType<StringAttr>("name");
      semantic::SVFormalArgumentSymbolOp formal =
          name ? formals.lookup(name.getValue())
               : semantic::SVFormalArgumentSymbolOp{};
      if (!formal)
        return constructor.emitError("constructor argument ")
               << name << " expanded by `default` is missing";
      forwarded.push_back(formal);
    }

    Location location = constructor.getLoc();
    if (!superCall) {
      // super.new must be the first statement (8.17); insert it before the
      // body's first statement.
      Block &members = constructor->getRegion(0).front();
      Operation *body = nullptr;
      for (Operation &member : members)
        if (!isa<SymbolOpInterface>(member)) {
          body = &member;
          break;
        }
      OpBuilder at(context);
      if (auto list = dyn_cast_or_null<semantic::SVStatementListOp>(body))
        at.setInsertionPointToStart(&list->getRegion(0).front());
      else if (body)
        at.setInsertionPoint(body);
      else
        at.setInsertionPointToStart(&members);
      Operation *statement = factory.create(
          at, location, semantic::SVExpressionStatementOp::getOperationName(),
          {});
      OpBuilder inside =
          OpBuilder::atBlockEnd(&statement->getRegion(0).front());
      superCall = cast<semantic::SVNewClassExpressionOp>(factory.create(
          inside, location,
          semantic::SVNewClassExpressionOp::getOperationName(),
          {builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
           builder.getNamedAttr("is_super_class", builder.getBoolAttr(true)),
           builder.getNamedAttr(
               "semantic_type",
               TypeAttr::get(semantic::VoidType::get(context)))}));
    }
    OpBuilder callBuilder =
        OpBuilder::atBlockEnd(&superCall->getRegion(0).front());
    Operation *call = factory.create(
        callBuilder, location, semantic::SVCallExpressionOp::getOperationName(),
        {builder.getNamedAttr("argument_count",
                              builder.getI64IntegerAttr(forwarded.size())),
         builder.getNamedAttr("callee_name", builder.getStringAttr("new")),
         builder.getNamedAttr("constraint_restrictions",
                              builder.getArrayAttr({})),
         builder.getNamedAttr("defaulted_arguments",
                              builder.getDenseI64ArrayAttr(
                                  SmallVector<int64_t>(forwarded.size(), 0))),
         builder.getNamedAttr("has_inline_constraints",
                              builder.getBoolAttr(false)),
         builder.getNamedAttr("has_iterator_expression",
                              builder.getBoolAttr(false)),
         builder.getNamedAttr("has_output_arguments",
                              builder.getBoolAttr(false)),
         builder.getNamedAttr("has_this_class", builder.getBoolAttr(false)),
         builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
         builder.getNamedAttr("is_super_class", builder.getBoolAttr(true)),
         builder.getNamedAttr("is_system_call", builder.getBoolAttr(false)),
         builder.getNamedAttr(
             "referenced_path",
             builder.getStringAttr(getHierarchyName(baseConstructor))),
         builder.getNamedAttr("referenced_symbol",
                              getSemanticSymbolPath(baseConstructor)),
         builder.getNamedAttr("semantic_type",
                              TypeAttr::get(semantic::VoidType::get(context))),
         builder.getNamedAttr(
             "subroutine_kind",
             semantic::SVSubroutineKindAttr::get(
                 context, baseConstructor.getSubroutineKind()))});
    OpBuilder argument = OpBuilder::atBlockEnd(&call->getRegion(0).front());
    for (semantic::SVFormalArgumentSymbolOp formal : forwarded) {
      auto type = formal->getAttrOfType<TypeAttr>("semantic_type");
      auto integral =
          type ? dyn_cast<semantic::IntegralType>(type.getValue()) : nullptr;
      factory.create(argument, location,
                     semantic::SVNamedValueExpressionOp::getOperationName(),
                     {builder.getNamedAttr(
                          "is_signed", builder.getBoolAttr(
                                           integral && integral.getIsSigned())),
                      builder.getNamedAttr(
                          "referenced_path",
                          builder.getStringAttr(getHierarchyName(formal))),
                      builder.getNamedAttr("referenced_symbol",
                                           getSemanticSymbolPath(formal)),
                      builder.getNamedAttr("semantic_type", type)});
    }
  }
  return success();
}

LogicalResult rewriteBuiltinBaseClasses(ModuleOp module) {
  MLIRContext *context = module.getContext();
  llvm::StringMap<semantic::SVClassTypeOp> classes;
  SmallVector<semantic::SVClassTypeOp> derived;
  module.walk([&](semantic::SVClassTypeOp classType) {
    classes[classType.getSymName()] = classType;
    if (std::optional<Type> base = classType.getBaseClass())
      if (auto handle = dyn_cast<semantic::ClassHandleType>(*base);
          handle && isBuiltinBaseClass(handle))
        derived.push_back(classType);
  });
  if (derived.empty())
    return success();

  SemanticOpFactory factory(module);
  Builder &builder = factory.builder;
  auto create = [&](OpBuilder &at, Location location, StringRef name,
                    ArrayRef<NamedAttribute> attributes) {
    return factory.create(at, location, name, attributes);
  };
  auto symbolPath = getSemanticSymbolPath;

  // The `$base` property of each derived class, keyed by class symbol.
  struct BuiltinBase {
    semantic::ClassHandleType type;
    std::string path;
    SymbolRefAttr symbol;
  };
  llvm::StringMap<BuiltinBase> bases;
  auto selectBase = [&](OpBuilder &at, Location location,
                        const BuiltinBase &base) {
    return create(
        at, location, semantic::SVNamedValueExpressionOp::getOperationName(),
        {builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
         builder.getNamedAttr("referenced_path",
                              builder.getStringAttr(base.path)),
         builder.getNamedAttr("referenced_symbol", base.symbol),
         builder.getNamedAttr("semantic_type", TypeAttr::get(base.type))});
  };
  for (semantic::SVClassTypeOp classType : derived) {
    auto type = cast<semantic::ClassHandleType>(*classType.getBaseClass());
    auto baseClass = classes.lookup(type.getClassName().getLeafReference());
    auto constructorPath =
        baseClass ? baseClass->getAttrOfType<StringAttr>("constructor_path")
                  : StringAttr{};
    auto constructorSymbol =
        baseClass
            ? baseClass->getAttrOfType<SymbolRefAttr>("constructor_symbol")
            : SymbolRefAttr{};
    auto hierarchy = classType->getAttrOfType<StringAttr>("hierarchical_name");
    if (!constructorPath || !constructorSymbol || !hierarchy)
      return classType.emitError("built-in base class has no constructor");
    Location location = classType.getLoc();
    Block &body = classType->getRegion(0).front();
    OpBuilder at(context);
    at.setInsertionPointToStart(&body);
    std::string path = (hierarchy.getValue() + "::$base").str();
    Operation *property = create(
        at, location, semantic::SVClassPropertySymbolOp::getOperationName(),
        {builder.getNamedAttr(
             SymbolTable::getSymbolAttrName(),
             builder.getStringAttr(classType.getSymName() + ".$base")),
         builder.getNamedAttr("hierarchical_name", builder.getStringAttr(path)),
         builder.getNamedAttr("name", builder.getStringAttr("$base")),
         builder.getNamedAttr("semantic_type", TypeAttr::get(type))});
    BuiltinBase base{type, path, symbolPath(property)};
    bases[classType.getSymName()] = base;
    classType->removeAttr("base_class");
    // super.new(...) creates the built-in base object.
    SmallVector<semantic::SVNewClassExpressionOp> superCalls;
    classType->walk([&](semantic::SVNewClassExpressionOp construct) {
      if (construct.getIsSuperClass() &&
          construct->getParentOfType<semantic::SVClassTypeOp>() == classType)
        superCalls.push_back(construct);
    });
    for (semantic::SVNewClassExpressionOp construct : superCalls) {
      OpBuilder before(construct);
      Operation *assignment =
          create(before, construct.getLoc(),
                 semantic::SVAssignmentExpressionOp::getOperationName(),
                 {builder.getNamedAttr("assignment_kind",
                                       builder.getI32IntegerAttr(0)),
                  builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
                  builder.getNamedAttr("semantic_type", TypeAttr::get(type))});
      OpBuilder inside =
          OpBuilder::atBlockEnd(&assignment->getRegion(0).front());
      selectBase(inside, construct.getLoc(), base);
      construct->moveAfter(&assignment->getRegion(0).front().back());
      construct->setAttr("is_super_class", builder.getBoolAttr(false));
      construct->setAttr("semantic_type", TypeAttr::get(type));
      for (Operation *child : getChildren(construct))
        if (isa<semantic::SVCallExpressionOp>(child))
          child->setAttr("is_super_class", builder.getBoolAttr(false));
    }
    if (!superCalls.empty())
      continue;
    // Without an explicit super.new, the implicit super.new() constructs the
    // base with its default argument: no keys, or an unbounded mailbox.
    OpBuilder initializer =
        OpBuilder::atBlockEnd(&property->getRegion(0).front());
    Operation *construct = create(
        initializer, location,
        semantic::SVNewClassExpressionOp::getOperationName(),
        {builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
         builder.getNamedAttr("is_super_class", builder.getBoolAttr(false)),
         builder.getNamedAttr("semantic_type", TypeAttr::get(type))});
    OpBuilder callBuilder =
        OpBuilder::atBlockEnd(&construct->getRegion(0).front());
    Operation *call = create(
        callBuilder, location, semantic::SVCallExpressionOp::getOperationName(),
        {builder.getNamedAttr("argument_count", builder.getI64IntegerAttr(1)),
         builder.getNamedAttr("callee_name", builder.getStringAttr("new")),
         builder.getNamedAttr("constraint_restrictions",
                              builder.getArrayAttr({})),
         builder.getNamedAttr("defaulted_arguments",
                              builder.getDenseI64ArrayAttr({1})),
         builder.getNamedAttr("has_inline_constraints",
                              builder.getBoolAttr(false)),
         builder.getNamedAttr("has_iterator_expression",
                              builder.getBoolAttr(false)),
         builder.getNamedAttr("has_output_arguments",
                              builder.getBoolAttr(false)),
         builder.getNamedAttr("has_this_class", builder.getBoolAttr(false)),
         builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
         builder.getNamedAttr("is_super_class", builder.getBoolAttr(false)),
         builder.getNamedAttr("is_system_call", builder.getBoolAttr(false)),
         builder.getNamedAttr("referenced_path", constructorPath),
         builder.getNamedAttr("referenced_symbol", constructorSymbol),
         builder.getNamedAttr("semantic_type",
                              TypeAttr::get(semantic::VoidType::get(context))),
         builder.getNamedAttr("subroutine_kind",
                              builder.getI32IntegerAttr(0))});
    OpBuilder argument = OpBuilder::atBlockEnd(&call->getRegion(0).front());
    create(
        argument, location, semantic::SVIntegerLiteralOp::getOperationName(),
        {builder.getNamedAttr("constant_value", builder.getStringAttr("0")),
         builder.getNamedAttr("is_declared_unsized", builder.getBoolAttr(true)),
         builder.getNamedAttr("is_signed", builder.getBoolAttr(true)),
         builder.getNamedAttr("semantic_type",
                              TypeAttr::get(semantic::IntegralType::get(
                                  context, 32, true, false, 31, 0,
                                  semantic::SVIntegralFlavor::Int)))});
  }

  // The built-in base held by a class or the nearest of its ancestors.
  auto findClassBase = [&](StringRef symbol) -> const BuiltinBase * {
    while (!symbol.empty()) {
      if (auto found = bases.find(symbol); found != bases.end())
        return &found->second;
      semantic::SVClassTypeOp classType = classes.lookup(symbol);
      std::optional<Type> base =
          classType ? classType.getBaseClass() : std::nullopt;
      auto handle = base ? dyn_cast<semantic::ClassHandleType>(*base) : nullptr;
      symbol = handle ? handle.getClassName().getLeafReference().getValue()
                      : StringRef{};
    }
    return nullptr;
  };
  auto findBase = [&](Type type) -> const BuiltinBase * {
    auto handle = dyn_cast_or_null<semantic::ClassHandleType>(type);
    return handle ? findClassBase(
                        handle.getClassName().getLeafReference().getValue())
                  : nullptr;
  };
  auto semanticType = [](Operation *operation) -> Type {
    auto type = operation->getAttrOfType<TypeAttr>("semantic_type");
    return type ? type.getValue() : Type{};
  };
  auto wrapInBase = [&](Operation *receiver, const BuiltinBase &base,
                        bool upcast) {
    OpBuilder before(receiver);
    Operation *access = create(
        before, receiver->getLoc(),
        semantic::SVMemberAccessExpressionOp::getOperationName(),
        {builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
         builder.getNamedAttr("member_name", builder.getStringAttr("$base")),
         builder.getNamedAttr("referenced_path",
                              builder.getStringAttr(base.path)),
         builder.getNamedAttr("referenced_symbol", base.symbol),
         builder.getNamedAttr("semantic_type", TypeAttr::get(base.type))});
    receiver->moveBefore(&access->getRegion(0).front(),
                         access->getRegion(0).front().end());
    if (upcast)
      access->setAttr(builtinBaseUpcastAttrName, builder.getUnitAttr());
  };

  // An inherited built-in method acts on the receiver's base object, and an
  // upcast to the built-in type selects that object.
  SmallVector<Operation *> rewrites;
  module.walk([&](Operation *operation) {
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(operation)) {
      auto path = call->getAttrOfType<StringAttr>("referenced_path");
      if (path && isBuiltinBaseMethodPath(path.getValue()) &&
          call.getCalleeName() != "new")
        rewrites.push_back(call);
    } else if (isa<semantic::SVConversionExpressionOp>(operation)) {
      auto handle =
          dyn_cast_or_null<semantic::ClassHandleType>(semanticType(operation));
      if (handle && isBuiltinBaseClass(handle))
        rewrites.push_back(operation);
    }
  });
  for (Operation *operation : rewrites) {
    SmallVector<Operation *> children = getChildren(operation);
    if (auto conversion =
            dyn_cast<semantic::SVConversionExpressionOp>(operation)) {
      if (children.size() != 1)
        continue;
      if (const BuiltinBase *base = findBase(semanticType(children.front())))
        wrapInBase(children.front(), *base, /*upcast=*/true);
      continue;
    }
    auto hasThis = operation->getAttrOfType<BoolAttr>("has_this_class");
    if (hasThis && hasThis.getValue()) {
      if (!children.empty())
        if (const BuiltinBase *base = findBase(semanticType(children.front())))
          wrapInBase(children.front(), *base, /*upcast=*/false);
      continue;
    }
    // An implicit `this` (or `super`) receiver selects the enclosing class's
    // base object.
    auto owner = operation->getParentOfType<semantic::SVClassTypeOp>();
    const BuiltinBase *base =
        owner ? findClassBase(owner.getSymName()) : nullptr;
    if (!base)
      continue;
    OpBuilder at = OpBuilder::atBlockBegin(&operation->getRegion(0).front());
    selectBase(at, operation->getLoc(), *base);
    operation->setAttr("has_this_class", builder.getBoolAttr(true));
  }
  return success();
}

semantic::SVSubroutineSymbolOp getClassMethod(Operation *member) {
  if (auto method = dyn_cast<semantic::SVSubroutineSymbolOp>(member))
    return method;
  if (auto prototype = dyn_cast<semantic::SVMethodPrototypeSymbolOp>(member))
    for (Operation *child : getChildren(prototype))
      if (auto method = dyn_cast<semantic::SVSubroutineSymbolOp>(child))
        return method;
  return {};
}

LogicalResult
materializeInheritedCovergroupPlans(semantic::SVRootSymbolOp semanticRoot,
                                    OpBuilder &builder) {
  llvm::StringMap<semantic::SVCovergroupTypeOp> groups;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp group) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(group.getSemanticType());
    if (handle)
      groups[handle.getCovergroupName().getLeafReference()] = group;
  });

  auto getBase = [&](semantic::SVCovergroupTypeOp group) {
    TypeAttr baseAttr = group.getBaseGroupAttr();
    auto handle =
        baseAttr ? dyn_cast<semantic::CovergroupHandleType>(baseAttr.getValue())
                 : semantic::CovergroupHandleType{};
    auto found =
        handle ? groups.find(handle.getCovergroupName().getLeafReference())
               : groups.end();
    return found == groups.end() ? semantic::SVCovergroupTypeOp{}
                                 : found->second;
  };
  auto getBody = [](semantic::SVCovergroupTypeOp group) {
    for (Operation *child : getChildren(group))
      if (auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(child))
        return body;
    return semantic::SVCovergroupBodySymbolOp{};
  };
  auto getSymbolReference = [](Operation *symbol) {
    SmallVector<StringAttr> path;
    for (Operation *current = symbol; current; current = current->getParentOp())
      if (isa<SymbolOpInterface>(current))
        path.push_back(SymbolTable::getSymbolName(current));
    assert(!path.empty() && "semantic symbol has no symbolic ancestor");
    std::reverse(path.begin(), path.end());
    SmallVector<FlatSymbolRefAttr> nested;
    nested.reserve(path.size() - 1);
    for (StringAttr name : ArrayRef(path).drop_front())
      nested.push_back(FlatSymbolRefAttr::get(name));
    return SymbolRefAttr::get(path.front(), nested);
  };

  llvm::SmallPtrSet<Operation *, 16> done;
  llvm::SmallPtrSet<Operation *, 16> active;
  std::function<LogicalResult(semantic::SVCovergroupTypeOp)> materialize =
      [&](semantic::SVCovergroupTypeOp derived) -> LogicalResult {
    if (done.contains(derived))
      return success();
    if (!active.insert(derived).second)
      return derived.emitError("covergroup inheritance contains a cycle");
    semantic::SVCovergroupTypeOp base = getBase(derived);
    if (!base) {
      active.erase(derived);
      done.insert(derived);
      return success();
    }
    if (failed(materialize(base)))
      return failure();
    auto baseBody = getBody(base);
    auto derivedBody = getBody(derived);
    if (!baseBody || !derivedBody)
      return derived.emitError("inherited covergroup has no effective body");

    llvm::StringSet<> directFormals;
    for (Operation *child : getChildren(derived))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
        directFormals.insert(getDebugName(formal));
    builder.setInsertionPoint(derivedBody);
    for (Operation *child : getChildren(base))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
        if (!directFormals.contains(getDebugName(formal)))
          builder.clone(*formal);
    for (Operation *child : getChildren(base))
      if (isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
              child))
        builder.clone(*child);

    llvm::StringSet<> directPoints;
    llvm::StringSet<> directPointSymbols;
    llvm::StringSet<> directCrosses;
    llvm::DenseSet<uint64_t> directOptions;
    for (Operation *member : getChildren(derivedBody)) {
      if (auto point = dyn_cast<semantic::SVCoverpointSymbolOp>(member)) {
        directPoints.insert(getDebugName(point));
        directPointSymbols.insert(point.getSymName());
      } else if (auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(member))
        directCrosses.insert(getDebugName(cross));
      else if (auto option = dyn_cast<semantic::SVCoverageOptionOp>(member))
        directOptions.insert(
            (uint64_t{static_cast<uint32_t>(option.getScopeKind())} << 32) |
            static_cast<uint32_t>(option.getOptionKind()));
    }

    Operation *insertionPoint =
        derivedBody->getRegion(0).front().empty()
            ? nullptr
            : &derivedBody->getRegion(0).front().front();
    if (insertionPoint)
      builder.setInsertionPoint(insertionPoint);
    else
      builder.setInsertionPointToEnd(&derivedBody->getRegion(0).front());
    uint64_t inheritedOptions = 0;
    llvm::StringMap<StringAttr> inheritedPointSymbols;
    for (Operation *member : getChildren(baseBody)) {
      if (auto option = dyn_cast<semantic::SVCoverageOptionOp>(member)) {
        uint64_t key =
            (uint64_t{static_cast<uint32_t>(option.getScopeKind())} << 32) |
            static_cast<uint32_t>(option.getOptionKind());
        if (!directOptions.contains(key)) {
          Operation *clone = builder.clone(*member);
          clone->setAttr("owner_symbol", getSymbolReference(derived));
          ++inheritedOptions;
        }
        continue;
      }
      auto point = dyn_cast<semantic::SVCoverpointSymbolOp>(member);
      if (!point)
        continue;
      Operation *clone = builder.clone(*member);
      auto clonedPoint = cast<semantic::SVCoverpointSymbolOp>(clone);
      if (directPoints.contains(getDebugName(point))) {
        cov::mark<cov::InheritedOverridden>(clone);
        if (!cov::has<cov::InheritedOriginType>(clone))
          cov::set<cov::InheritedOriginType>(
              clone, builder.getI64IntegerAttr(stableFunctionalTypeID(base)));
      }
      if (directPointSymbols.contains(point.getSymName())) {
        auto origin = cov::get<cov::InheritedOriginType>(clone);
        std::string uniqueName =
            (Twine(point.getSymName()) + "$inherited$" +
             Twine(origin ? origin.getValue().getZExtValue()
                          : stableFunctionalTypeID(base)))
                .str();
        clonedPoint.setSymName(builder.getStringAttr(uniqueName));
      }
      SymbolRefAttr clonedPointReference = getSymbolReference(clonedPoint);
      clonedPoint->walk([&](semantic::SVCoverageOptionOp option) {
        option->setAttr("owner_symbol", clonedPointReference);
      });
      inheritedPointSymbols[point.getSymName()] = clonedPoint.getSymNameAttr();
    }
    for (Operation *member : getChildren(baseBody)) {
      auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(member);
      if (!cross || directCrosses.contains(getDebugName(cross)))
        continue;
      Operation *clone = builder.clone(*member);
      auto clonedCross = cast<semantic::SVCoverCrossSymbolOp>(clone);
      SymbolRefAttr clonedCrossReference = getSymbolReference(clonedCross);
      clonedCross->walk([&](Operation *nested) {
        if (isa<semantic::SVCoverageOptionOp>(nested))
          nested->setAttr("owner_symbol", clonedCrossReference);
        if (nested->hasTrait<OpTrait::CoverageSelectorNode>())
          nested->setAttr("enclosing_cross_symbol", clonedCrossReference);
      });
      SmallVector<Attribute> effectiveTargets;
      effectiveTargets.reserve(cross.getTargetSymbols().size());
      ArrayAttr baseEffectiveTargets =
          cov::get<cov::EffectiveTargetSymbols>(cross);
      for (auto [ordinal, targetAttr] :
           llvm::enumerate(cross.getTargetSymbols())) {
        auto target = dyn_cast<SymbolRefAttr>(targetAttr);
        StringRef sourceTarget =
            target ? target.getLeafReference() : StringRef{};
        if (baseEffectiveTargets && ordinal < baseEffectiveTargets.size())
          if (auto inherited =
                  dyn_cast<StringAttr>(baseEffectiveTargets[ordinal]))
            sourceTarget = inherited.getValue();
        auto effective = inheritedPointSymbols.find(sourceTarget);
        effectiveTargets.push_back(effective == inheritedPointSymbols.end()
                                       ? builder.getStringAttr(sourceTarget)
                                       : Attribute(effective->second));
      }
      cov::set<cov::EffectiveTargetSymbols>(
          clone, builder.getArrayAttr(effectiveTargets));
      clone->walk([&](Operation *nested) {
        auto target = nested->getAttrOfType<SymbolRefAttr>("target_symbol");
        StringRef sourceTarget =
            target ? target.getLeafReference() : StringRef{};
        if (auto inherited = cov::get<cov::EffectiveTargetSymbol>(nested))
          sourceTarget = inherited.getValue();
        auto effective = inheritedPointSymbols.find(sourceTarget);
        if (effective != inheritedPointSymbols.end())
          cov::set<cov::EffectiveTargetSymbol>(nested, effective->second);
      });
    }
    derivedBody->setAttr("option_count",
                         builder.getI64IntegerAttr(
                             derivedBody.getOptionCount() + inheritedOptions));
    cov::set<cov::BaseGroup>(derived, derived.getBaseGroupAttr());
    derived->removeAttr("base_group");
    derived->setAttr("coverage_event_kind",
                     builder.getI32IntegerAttr(
                         static_cast<uint32_t>(base.getCoverageEventKind())));
    active.erase(derived);
    done.insert(derived);
    return success();
  };

  for (const auto &entry : groups)
    if (failed(materialize(entry.second)))
      return failure();
  return success();
}

LogicalResult materializeCovergroupTypeOptionInitializers(
    semantic::SVRootSymbolOp semanticRoot, OpBuilder &builder,
    const llvm::DenseSet<Type> &unusedEmbeddedCovergroupTypes) {
  bool invalid = false;
  llvm::DenseMap<Operation *, uint64_t> typeIDs;
  llvm::DenseMap<Operation *, StringAttr> storagePaths;
  llvm::StringMap<Operation *> storageOwners;
  // Each owner has at most seven fields. Visit its definitions once and use
  // an owner-local index, rather than searching the design for each field.
  semanticRoot->walk([&](semantic::SVClassPropertySymbolOp property) {
    if (property.getName() != "type_option" ||
        !property.getIsCompilerGenerated().value_or(false) ||
        property.getLifetime() != semantic::SVVariableLifetime::Static)
      return;
    Operation *owner = property->getParentOp();
    if (!isa<semantic::SVCovergroupBodySymbolOp, semantic::SVCoverpointSymbolOp,
             semantic::SVCoverCrossSymbolOp>(owner))
      return;
    auto group = property->getParentOfType<semantic::SVCovergroupTypeOp>();
    if (!group ||
        unusedEmbeddedCovergroupTypes.contains(group.getSemanticType()))
      return;
    auto [type, inserted] = typeIDs.try_emplace(group);
    if (inserted)
      type->second = stableFunctionalTypeID(group);
    // Slang's display path omits the anonymous embedded group name; inherited
    // item clones also retain their base's display path. Neither is a storage
    // identity. Salt the compiler-owned static root by its group and item.
    const uint64_t itemID =
        isa<semantic::SVCovergroupBodySymbolOp>(owner)
            ? 0
            : stableCodeUnitID(functionalSemanticIdentity(owner));
    auto path = builder.getStringAttr((Twine(getHierarchyName(property)) +
                                       ".$coverage$" + Twine(type->second) +
                                       "$" + Twine(itemID))
                                          .str());
    auto [previous, unique] =
        storageOwners.try_emplace(path.getValue(), property);
    if (!unique) {
      property.emitError("static type_option storage identity collision with ")
          << previous->second->getLoc();
      invalid = true;
      return;
    }
    storagePaths.try_emplace(property, path);
    property->setAttr("hierarchical_name", path);
    auto aggregate = dyn_cast_or_null<semantic::SourceAggregateType>(
        property.getSemanticType().value_or(Type{}));
    if (!aggregate || !getChildren(property).empty()) {
      property.emitError(
          "implicit type_option requires an uninitialized struct");
      invalid = true;
      return;
    }
    llvm::DenseMap<semantic::SVCoverageOptionKind, semantic::SVCoverageOptionOp>
        definitions;
    for (Operation *child : getChildren(owner))
      if (auto option = dyn_cast<semantic::SVCoverageOptionOp>(child);
          option &&
          option.getScopeKind() == semantic::SVCoverageOptionScopeKind::Type)
        definitions.try_emplace(option.getOptionKind(), option);

    OpBuilder::InsertionGuard guard(builder);
    if (property.getBody().empty())
      property.getBody().emplaceBlock();
    SmallVector<int64_t> ordinals;
    for (auto [ordinal, attribute] : llvm::enumerate(aggregate.getFields())) {
      auto field = cast<DictionaryAttr>(attribute);
      StringRef name = field.getAs<StringAttr>("name").getValue();
      Type type = field.getAs<TypeAttr>("type").getValue();
      auto kind =
          llvm::StringSwitch<std::optional<semantic::SVCoverageOptionKind>>(
              name)
              .Case("weight", semantic::SVCoverageOptionKind::Weight)
              .Case("goal", semantic::SVCoverageOptionKind::Goal)
              .Case("comment", semantic::SVCoverageOptionKind::Comment)
              .Case("strobe", semantic::SVCoverageOptionKind::Strobe)
              .Case("merge_instances",
                    semantic::SVCoverageOptionKind::MergeInstances)
              .Case("distribute_first",
                    semantic::SVCoverageOptionKind::DistributeFirst)
              .Case("real_interval",
                    semantic::SVCoverageOptionKind::RealInterval)
              .Default(std::nullopt);
      if (!kind) {
        property.emitError("unknown implicit type_option field '")
            << name << "'";
        invalid = true;
        continue;
      }
      auto found = definitions.find(*kind);
      auto definition = found == definitions.end()
                            ? semantic::SVCoverageOptionOp{}
                            : found->second;
      NamedAttrList attrs;
      StringAttr spelling;
      Location location = getSemanticLocation(property);
      if (definition) {
        SmallVector<Operation *> expressions = getChildren(definition);
        if (expressions.size() != 1) {
          definition.emitError(
              "type option definition requires one expression");
          invalid = true;
          continue;
        }
        Operation *expression = expressions.front();
        location = getSemanticLocation(expression);
        // Copy source provenance, without carrying call or conversion
        // attributes onto the newly typed literal operation.
        for (NamedAttribute attr : expression->getAttrs()) {
          StringRef key = attr.getName().getValue();
          if (key == "node_id" || key.starts_with("source_") ||
              key == "original_source_range" ||
              key == "macro_expansion_stack" || key == "macro_name")
            attrs.append(attr);
        }
        spelling = cov::get<cov::TypeOptionConstant>(definition);
        if (!spelling)
          if (auto constant = getConstantSpelling(expression))
            spelling = builder.getStringAttr(*constant);
        // Also accept hand-written semantic IR with a literal conversion.
        for (Operation *value = expression; !spelling;) {
          spelling = value->getAttrOfType<StringAttr>("constant_value");
          auto children = getChildren(value);
          if (!isa<semantic::SVConversionExpressionOp>(value) ||
              children.size() != 1)
            break;
          value = children.front();
        }
        if (!spelling) {
          definition.emitError(
              "type option definition has no elaborated constant");
          invalid = true;
          continue;
        }
      } else {
        attrs.set("node_id", property.getNodeIdAttr());
        spelling = builder.getStringAttr(
            name == "weight" || name == "real_interval" ? "1"
            : name == "goal"                            ? "100"
            : name == "comment"                         ? ""
                                                        : "0");
      }
      attrs.set("semantic_type", TypeAttr::get(type));
      attrs.set("constant_value", spelling);
      attrs.set("is_signed", builder.getBoolAttr(isSignedSemanticType(type)));
      attrs.erase(foldedConstantAttrName);
      auto literal = [&]() -> Operation * {
        Operation *result;
        if (isa<semantic::StringType>(type))
          result = semantic::SVStringLiteralOp::create(
              builder, location, TypeRange{}, ValueRange{}, attrs.getAttrs());
        else if (isa<semantic::RealType>(type))
          result = semantic::SVRealLiteralOp::create(
              builder, location, TypeRange{}, ValueRange{}, attrs.getAttrs());
        else
          result = semantic::SVIntegerLiteralOp::create(
              builder, location, TypeRange{}, ValueRange{}, attrs.getAttrs());
        result->getRegion(0).emplaceBlock();
        return result;
      };
      // Freeze the same elaborated value in constructor batches. In
      // particular, a constant function must never run once per instance.
      if (definition) {
        builder.setInsertionPointToStart(&definition.getBody().front());
        Operation *original = getChildren(definition).front();
        literal();
        original->erase();
      }
      builder.setInsertionPointToEnd(&property.getBody().front());
      Operation *initializer = literal();
      ordinals.push_back(ordinal);
      std::optional<sim::CovergroupInstanceOptionKind> publish;
      if (definition) {
        if (name == "weight")
          publish = sim::CovergroupInstanceOptionKind::Weight;
        else if (name == "goal")
          publish = sim::CovergroupInstanceOptionKind::Goal;
        else if (name == "comment")
          publish = sim::CovergroupInstanceOptionKind::Comment;
        else if (name == "merge_instances")
          publish = sim::CovergroupInstanceOptionKind::MergeInstances;
        else if (name == "distribute_first")
          publish = sim::CovergroupInstanceOptionKind::DistributeFirst;
      }
      if (publish) {
        sim::set<sim::metadata::InitializeTypeOptionOwner>(
            initializer, definition.getOwnerSymbolAttr());
        sim::set<sim::metadata::InitializeTypeOptionKind>(
            initializer, sim::CovergroupInstanceOptionKindAttr::get(
                             builder.getContext(), *publish));
      }
    }
    property->setAttr("obelisk.aggregate_member_initializer_ordinals",
                      builder.getDenseI64ArrayAttr(ordinals));
  });
  // Freeze source references to the same independent storage roots. Qualified
  // symbols are authoritative; each lookup reuses one preparation-local table
  // collection instead of scanning owners by their ambiguous display paths.
  SymbolTableCollection symbols;
  ModuleOp module = semanticRoot->getParentOfType<ModuleOp>();
  semanticRoot->walk([&](Operation *operation) {
    auto reference =
        operation->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    if (!reference)
      return;
    Operation *property = symbols.lookupSymbolIn(module, reference);
    auto path = storagePaths.find(property);
    if (path != storagePaths.end()) {
      operation->setAttr("referenced_path", path->second);
      if (isa<semantic::SVMemberAccessExpressionOp>(operation) &&
          isa<semantic::SVCoverpointSymbolOp, semantic::SVCoverCrossSymbolOp>(
              property->getParentOp()))
        sim::mark<sim::metadata::TypeOptionItemQualifier>(operation);
    }
  });
  return failure(invalid);
}

LogicalResult materializeCovergroupDeclarations(
    semantic::SVRootSymbolOp semanticRoot, OpBuilder &builder,
    const llvm::DenseSet<Type> &unusedEmbeddedCovergroupTypes) {
  SmallVector<semantic::SVCovergroupTypeOp> covergroupSources;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp covergroup) {
    if (!unusedEmbeddedCovergroupTypes.contains(covergroup.getSemanticType()))
      covergroupSources.push_back(covergroup);
  });
  llvm::sort(covergroupSources, [](semantic::SVCovergroupTypeOp lhs,
                                   semantic::SVCovergroupTypeOp rhs) {
    return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
           std::tuple(getHierarchyName(rhs), rhs.getSymName());
  });

  bool invalid = false;
  llvm::DenseMap<uint64_t, Operation *> typeIDs;
  for (semantic::SVCovergroupTypeOp covergroup : covergroupSources) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(covergroup.getSemanticType());
    if (!handle) {
      emitError(getSemanticLocation(covergroup))
          << "covergroup declaration has no handle type";
      invalid = true;
      continue;
    }
    const uint64_t typeID = stableFunctionalTypeID(covergroup);
    auto [typeOwner, insertedType] =
        typeIDs.try_emplace(typeID, covergroup.getOperation());
    if (!insertedType) {
      emitError(getSemanticLocation(covergroup))
          << "functional coverage type ID hash collision with "
          << typeOwner->second->getLoc();
      invalid = true;
      continue;
    }
    StringAttr symbol =
        getSimulationCovergroupSymbol(handle.getCovergroupName());
    FlatSymbolRefAttr base;
    TypeAttr baseAttr = cov::get<cov::BaseGroup>(covergroup);
    if (baseAttr) {
      auto baseHandle =
          dyn_cast<semantic::CovergroupHandleType>(baseAttr.getValue());
      if (!baseHandle) {
        emitError(getSemanticLocation(covergroup))
            << "covergroup base is not a covergroup handle";
        invalid = true;
        continue;
      }
      base = FlatSymbolRefAttr::get(
          getSimulationCovergroupSymbol(baseHandle.getCovergroupName()));
    }
    auto declaration = sim::SimCovergroupDeclOp::create(
        builder, getSemanticLocation(covergroup), symbol, typeID, base,
        builder.getStringAttr(getDebugName(covergroup)));
    SymbolTable::setSymbolVisibility(declaration,
                                     SymbolTable::Visibility::Public);
  }
  return failure(invalid);
}

FailureOr<PreparedClassDeclarations> materializeClassDeclarations(
    ModuleOp module, sim::SimDesignOp design,
    semantic::SVRootSymbolOp semanticRoot, OpBuilder &builder,
    const llvm::StringMap<Operation *> &semanticSymbols,
    const llvm::DenseSet<Type> &unusedEmbeddedCovergroupTypes) {
  MLIRContext *context = module.getContext();
  PreparedClassDeclarations result;
  // Inventory classes and detect the feature in one traversal. Designs that
  // do not use class bit-stream casts retain their previous declaration walk
  // and receive no extra field metadata.
  bool needsClassBitstreamMetadata = false;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *operation) {
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(operation)) {
      if (classType.getIsUninstantiated())
        return WalkResult::skip();
      result.sources.push_back(classType);
      return WalkResult::advance();
    }
    if (auto streaming =
            dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(
                operation)) {
      SmallVector<Operation *> children = getChildren(streaming);
      size_t next = 0;
      for (int64_t withFlag : streaming.getStreamWithFlags()) {
        if (next >= children.size())
          break;
        Operation *child = children[next++];
        auto semanticSource = child->getAttrOfType<TypeAttr>("semantic_type");
        if (semanticSource &&
            isa<semantic::VoidType>(semanticSource.getValue())) {
          if (withFlag != 0)
            ++next;
          continue;
        }
        FailureOr<Type> source = getNormalizedSemanticType(child);
        bool containsClass =
            succeeded(source) && containsClassBitstreamSource(*source);
        needsClassBitstreamMetadata |= containsClass;
        if (containsClass && isExactCurrentInstanceThis(child))
          sim::mark<sim::metadata::ClassBitstreamAllowHiddenRoot>(streaming);
        if (withFlag != 0)
          ++next;
      }
      return WalkResult::advance();
    }
    auto conversion = dyn_cast<semantic::SVConversionExpressionOp>(operation);
    if (!conversion)
      return WalkResult::advance();
    BoolAttr implicit = conversion->getAttrOfType<BoolAttr>("is_implicit");
    if (!implicit || implicit.getValue())
      return WalkResult::advance();
    SmallVector<Operation *> children = getChildren(conversion);
    if (children.size() != 1)
      return WalkResult::advance();
    // IEEE 1800-2017 13.4.1 permits an explicit cast to void to discard a
    // function result. This inventory walk only detects class bit-stream
    // casts, so a void target is unrelated and must not be normalized as a
    // simulation value type.
    auto semanticTarget = conversion->getAttrOfType<TypeAttr>("semantic_type");
    if (semanticTarget && isa<semantic::VoidType>(semanticTarget.getValue()))
      return WalkResult::advance();
    FailureOr<Type> target = getNormalizedSemanticType(conversion);
    if (failed(target) ||
        (!sim::getPackedScalarType(*target) &&
         !isa<sim::StringType, sim::DynamicArrayType, sim::QueueType,
              sim::UnpackedArrayType, sim::UnpackedStructType>(*target)))
      return WalkResult::advance();
    auto semanticSource =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (semanticSource && isa<semantic::VoidType>(semanticSource.getValue()))
      return WalkResult::advance();
    FailureOr<Type> source = getNormalizedSemanticType(children.front());
    bool containsClass =
        succeeded(source) && containsClassBitstreamSource(*source);
    needsClassBitstreamMetadata |= containsClass;
    if (containsClass && isExactCurrentInstanceThis(children.front()))
      sim::mark<sim::metadata::ClassBitstreamAllowHiddenRoot>(conversion);
    return WalkResult::advance();
  });
  if (needsClassBitstreamMetadata)
    sim::mark<sim::metadata::ClassBitstreamSource>(module);
  if (needsClassBitstreamMetadata) {
    llvm::DenseMap<Type, uint64_t> fixedClassWidths;
    for (semantic::SVClassTypeOp classType : result.sources)
      if (classType.getBitstreamWidth() != 0)
        fixedClassWidths.try_emplace(classType.getSemanticType(),
                                     classType.getBitstreamWidth());
    semanticRoot->walk([&](Operation *operation) {
      auto semanticType = operation->getAttrOfType<TypeAttr>("semantic_type");
      auto found = semanticType ? fixedClassWidths.find(semanticType.getValue())
                                : fixedClassWidths.end();
      if (found != fixedClassWidths.end())
        sim::set<sim::metadata::ClassBitstreamWidth>(
            operation, builder.getI64IntegerAttr(found->second));
    });
  }
  // The IEEE weak_reference specializations live in the standard package,
  // outside the elaborated source root, but their handles can occur in source
  // storage and function signatures.
  module.walk([&](semantic::SVClassTypeOp classType) {
    if (!classType.getIsUninstantiated() && isWeakReferenceClass(classType) &&
        !llvm::is_contained(result.sources, classType))
      result.sources.push_back(classType);
  });
  llvm::sort(result.sources,
             [](semantic::SVClassTypeOp lhs, semantic::SVClassTypeOp rhs) {
               return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
                      std::tuple(getHierarchyName(rhs), rhs.getSymName());
             });

  llvm::DenseMap<Operation *, uint64_t> classIDs;
  for (auto [index, classType] : llvm::enumerate(result.sources))
    classIDs[classType] = index + 1;
  for (semantic::SVClassTypeOp classType : result.sources) {
    auto handle = cast<semantic::ClassHandleType>(classType.getSemanticType());
    result.semanticClasses[handle.getClassName().getLeafReference()] =
        classType;
  }

  // rand_mode property indices are defined over the effective base-to-derived
  // property sequence. Freeze the same stable index on each instance field so
  // later ABI preparation can describe direct rand-object edges without
  // retaining the semantic class tree.
  llvm::DenseMap<Operation *, uint64_t> randomPropertyCounts;
  llvm::SmallPtrSet<Operation *, 8> countingRandomProperties;
  std::function<FailureOr<uint64_t>(semantic::SVClassTypeOp)>
      countRandomProperties =
          [&](semantic::SVClassTypeOp classType) -> FailureOr<uint64_t> {
    if (auto found = randomPropertyCounts.find(classType);
        found != randomPropertyCounts.end())
      return found->second;
    if (!countingRandomProperties.insert(classType).second)
      return failure();
    uint64_t count = 0;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      if (base == result.semanticClasses.end())
        return failure();
      FailureOr<uint64_t> baseCount = countRandomProperties(base->second);
      if (failed(baseCount))
        return failure();
      count = *baseCount;
    }
    for (Operation *child : getChildren(classType))
      if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child))
        if (property.getRandMode() != semantic::SVRandMode::None) {
          ++count;
          auto sourceType = property.getSemanticType();
          auto structure =
              sourceType ? dyn_cast<semantic::SourceAggregateType>(*sourceType)
                         : semantic::SourceAggregateType{};
          if (structure && !structure.getIsPacked() &&
              !structure.getIsUnion()) {
            auto members = collectQualifiedRandomStructMembers(
                structure, getSemanticLocation(property), property);
            if (failed(members))
              return failure();
            count += members->size();
          }
        }
    countingRandomProperties.erase(classType);
    randomPropertyCounts[classType] = count;
    return count;
  };

  auto classReference = [&](Type type) -> FlatSymbolRefAttr {
    auto handle = dyn_cast<semantic::ClassHandleType>(type);
    return handle ? FlatSymbolRefAttr::get(
                        getSimulationClassSymbol(handle.getClassName()))
                  : FlatSymbolRefAttr{};
  };

  llvm::DenseMap<Operation *, Operation *> inheritedCovergroupProperties;
  llvm::StringMap<semantic::SVCovergroupTypeOp> covergroupsBySymbol;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp covergroup) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(covergroup.getSemanticType());
    if (handle)
      covergroupsBySymbol[handle.getCovergroupName().getLeafReference()] =
          covergroup;
  });
  auto findCovergroupProperty = [](semantic::SVCovergroupTypeOp covergroup)
      -> semantic::SVClassPropertySymbolOp {
    auto owner = covergroup->getParentOfType<semantic::SVClassTypeOp>();
    if (!owner)
      return {};
    for (Operation *child : getChildren(owner))
      if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child))
        if (property.getSemanticType() == covergroup.getSemanticType())
          return property;
    return {};
  };
  for (const auto &entry : covergroupsBySymbol) {
    semantic::SVCovergroupTypeOp derived = entry.second;
    TypeAttr baseAttr = cov::get<cov::BaseGroup>(derived);
    if (!baseAttr)
      continue;
    auto baseHandle =
        dyn_cast<semantic::CovergroupHandleType>(baseAttr.getValue());
    auto base = baseHandle
                    ? covergroupsBySymbol.find(
                          baseHandle.getCovergroupName().getLeafReference())
                    : covergroupsBySymbol.end();
    auto derivedProperty = findCovergroupProperty(derived);
    auto baseProperty = base == covergroupsBySymbol.end()
                            ? semantic::SVClassPropertySymbolOp{}
                            : findCovergroupProperty(base->second);
    if (!derivedProperty || !baseProperty) {
      emitError(getSemanticLocation(derived))
          << "embedded covergroup inheritance has no corresponding class "
             "property";
      return failure();
    }
    inheritedCovergroupProperties[derivedProperty] = baseProperty;
  }

  bool invalid = false;
  llvm::DenseMap<Operation *, sim::SimClassFieldDeclOp> fieldDeclarations;
  for (semantic::SVClassTypeOp classType : result.sources) {
    uint64_t randomPropertyIndex = 0;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      FailureOr<uint64_t> baseCount = base == result.semanticClasses.end()
                                          ? FailureOr<uint64_t>(failure())
                                          : countRandomProperties(base->second);
      if (failed(baseCount)) {
        emitError(getSemanticLocation(classType))
            << "cannot determine inherited rand_mode property indices";
        invalid = true;
      } else {
        randomPropertyIndex = *baseCount;
      }
    }
    FlatSymbolRefAttr base;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      base = classReference(*baseType);
      if (!base) {
        emitError(getSemanticLocation(classType))
            << "class base is not a class handle";
        invalid = true;
      }
    }
    SmallVector<Attribute> interfaces;
    for (Attribute attribute : classType.getImplementedInterfaces()) {
      auto type = dyn_cast<TypeAttr>(attribute);
      FlatSymbolRefAttr interface =
          type ? classReference(type.getValue()) : FlatSymbolRefAttr{};
      if (!interface) {
        emitError(getSemanticLocation(classType))
            << "implemented interface is not a class handle";
        invalid = true;
        continue;
      }
      interfaces.push_back(interface);
    }
    auto semanticClassType =
        cast<semantic::ClassHandleType>(classType.getSemanticType());
    StringAttr classSymbol =
        getSimulationClassSymbol(semanticClassType.getClassName());
    result.symbols[classType] = classSymbol;
    FlatSymbolRefAttr weakReferent;
    if (isWeakReferenceClass(classType))
      for (Operation *child : getChildren(classType))
        if (auto parameter =
                dyn_cast<semantic::SVTypeParameterSymbolOp>(child)) {
          if (auto type = parameter->getAttrOfType<TypeAttr>("semantic_type"))
            weakReferent = classReference(type.getValue());
          break;
        }
    bool hasExplicitConstructor =
        llvm::any_of(getChildren(classType), [](Operation *child) {
          semantic::SVSubroutineSymbolOp method = getClassMethod(child);
          return method && method.getIsConstructor().value_or(false);
        });
    FlatSymbolRefAttr implicitConstructor;
    if (!hasExplicitConstructor && !classType.getIsInterface())
      implicitConstructor = FlatSymbolRefAttr::get(
          context, (classSymbol.getValue() + "_implicit_new").str());
    auto declaration = sim::SimClassDeclOp::create(
        builder, getSemanticLocation(classType), classSymbol,
        classIDs.lookup(classType), base,
        interfaces.empty() ? ArrayAttr{} : builder.getArrayAttr(interfaces),
        weakReferent, ArrayAttr{}, FlatSymbolRefAttr{}, implicitConstructor,
        classType.getIsAbstract() || classType.getIsInterface(),
        classType.getIsInterface(), classType.getIsFinal(),
        builder.getStringAttr(getDebugName(classType)));
    result.declarations[classType] = declaration;
    // A class whose data members all have fixed bit-stream widths streams that
    // many bits from any non-null handle (IEEE 1800-2023 11.4.14.1).
    if (needsClassBitstreamMetadata && classType.getBitstreamWidth() != 0)
      sim::set<sim::metadata::ClassBitstreamWidth>(
          declaration,
          builder.getI64IntegerAttr(classType.getBitstreamWidth()));
    // Class inventory is part of the executable ABI. Keep descriptors even
    // when the only current reference is embedded in a type.
    SymbolTable::setSymbolVisibility(declaration,
                                     SymbolTable::Visibility::Public);
    if (implicitConstructor)
      result.implicitConstructorSymbols[classType] = implicitConstructor;

    uint64_t ordinal = 0;
    struct CyclicDomainField {
      Location location;
      std::string name;
      Type type;
    };
    SmallVector<CyclicDomainField> cyclicDomainFields;
    auto reserveCyclicDomain = [&](Location location, std::string name,
                                   Type valueType) {
      auto symbol = FlatSymbolRefAttr::get(context, name);
      cyclicDomainFields.push_back(
          {location, std::move(name),
           getRandomCyclicStateType(valueType, sim::StringType::get(context))});
      return symbol;
    };
    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      if (!property)
        continue;
      if (inheritedCovergroupProperties.contains(property))
        continue;
      if (std::optional<Type> type = property.getSemanticType();
          type && unusedEmbeddedCovergroupTypes.contains(*type))
        continue;
      FailureOr<Type> type = getNormalizedSemanticType(property);
      if (failed(type)) {
        invalid = true;
        continue;
      }
      bool isStatic =
          property.getLifetime() == semantic::SVVariableLifetime::Static;
      SmallVector<QualifiedRandomMember> structMembers;
      auto propertySemanticType = property.getSemanticType();
      auto sourceStructure =
          propertySemanticType
              ? dyn_cast<semantic::SourceAggregateType>(*propertySemanticType)
              : semantic::SourceAggregateType{};
      if (sourceStructure && !sourceStructure.getIsPacked() &&
          !sourceStructure.getIsUnion()) {
        if (property.getRandMode() == semantic::SVRandMode::RandC) {
          emitError(getSemanticLocation(property))
              << "unpacked structures shall not be randc";
          invalid = true;
          continue;
        }
        auto members = collectQualifiedRandomStructMembers(
            sourceStructure, getSemanticLocation(property), property);
        if (failed(members)) {
          invalid = true;
          continue;
        }
        structMembers = std::move(*members);
      }
      std::string fieldName =
          (classSymbol.getValue() + "_field_" + llvm::Twine(ordinal)).str();
      FlatSymbolRefAttr fieldSymbol =
          FlatSymbolRefAttr::get(context, fieldName);
      result.fieldSymbols[property] = fieldSymbol;
      auto field = sim::SimClassFieldDeclOp::create(
          builder, getSemanticLocation(property), fieldName, classSymbol, *type,
          ordinal++, IntegerAttr{}, isStatic,
          /*isWeak=*/false, builder.getStringAttr(getDebugName(property)));
      // IEEE 1800-2023 6.24.3 and 11.4.14.1 stream the data members of the
      // referenced object. Static properties have one class-wide copy (8.9),
      // so they are not object members. Keep the exact source-property marker
      // separate from the physical class layout: synthetic RNG/rand_mode
      // fields must never leak into a language bit stream either.
      if (needsClassBitstreamMetadata && !isStatic) {
        sim::mark<sim::metadata::ClassBitstreamMember>(field);
        sim::set<sim::metadata::ClassBitstreamVisibility>(
            field,
            sim::MemberVisibilityAttr::get(
                builder.getContext(), static_cast<sim::MemberVisibility>(
                                          property.getMemberVisibility())));
      }
      // IEEE 1800-2023 19.4: a covergroup declared in the class is embedded,
      // and its implicit property is the one a shallow copy sets to null.
      if (auto handle = dyn_cast_or_null<semantic::CovergroupHandleType>(
              propertySemanticType ? *propertySemanticType : Type{})) {
        auto covergroup = covergroupsBySymbol.find(
            handle.getCovergroupName().getLeafReference());
        if (!isStatic && covergroup != covergroupsBySymbol.end() &&
            covergroup->second->getParentOp() == classType)
          sim::mark<sim::metadata::EmbeddedCovergroup>(field);
      }
      fieldDeclarations[property] = field;
      if (property.getRandMode() != semantic::SVRandMode::None) {
        sim::set<sim::metadata::RandomModeIndex>(
            field, builder.getI64IntegerAttr(randomPropertyIndex++));
        if (!isStatic && isa<sim::ClassHandleType>(*type))
          sim::mark<sim::metadata::RandomObjectEdge>(field);
        if (!isStatic && sim::getPackedWidth(*type)) {
          sim::RandomVariableKind kind = sim::RandomVariableKind::Rand;
          switch (property.getRandMode()) {
          case semantic::SVRandMode::Rand:
            kind = sim::RandomVariableKind::Rand;
            break;
          case semantic::SVRandMode::RandC:
            kind = sim::RandomVariableKind::RandC;
            break;
          case semantic::SVRandMode::None:
            llvm_unreachable("non-random property reached random metadata");
          }
          sim::set<sim::metadata::RandomVariableKind>(
              field, sim::RandomVariableKindAttr::get(context, kind));
          std::optional<Type> semanticType = property.getSemanticType();
          if (!semanticType) {
            emitError(getSemanticLocation(property))
                << "random property has no semantic type";
            invalid = true;
          } else {
            sim::set<sim::metadata::RandomVariableSigned>(
                field,
                builder.getBoolAttr(isSignedSemanticType(*semanticType)));
          }
        }
      }
      if (!isStatic && property.getRandMode() == semantic::SVRandMode::RandC) {
        auto addRandCField = [&](StringRef suffix, StringRef debugName) {
          std::string name = (fieldName + suffix).str();
          FlatSymbolRefAttr symbol = FlatSymbolRefAttr::get(context, name);
          sim::SimClassFieldDeclOp::create(
              builder, getSemanticLocation(property), name, classSymbol,
              getRandomCyclicStateType(*type, builder.getI64Type()), ordinal++,
              IntegerAttr{},
              /*isStatic=*/false, /*isWeak=*/false,
              builder.getStringAttr(debugName));
          return symbol;
        };
        result.randcKeyFieldSymbols[property] =
            addRandCField("_randc_key", "__obelisk_randc_key");
        result.randcPositionFieldSymbols[property] =
            addRandCField("_randc_position", "__obelisk_randc_position");
        result.randcDomainFieldSymbols[property] = reserveCyclicDomain(
            getSemanticLocation(property), fieldName + "_randc_domain", *type);
        if (sim::has<sim::metadata::RandomVariableKind>(field)) {
          sim::set<sim::metadata::RandomCycleKeyField>(
              field, result.randcKeyFieldSymbols.lookup(property));
          sim::set<sim::metadata::RandomCyclePositionField>(
              field, result.randcPositionFieldSymbols.lookup(property));
        }
      }
      if (!isStatic && property.getRandMode() != semantic::SVRandMode::None)
        if (auto layout = getFixedRandomArrayLayout(*type)) {
          std::string name = fieldName + "_element_modes";
          FlatSymbolRefAttr symbol = FlatSymbolRefAttr::get(context, name);
          sim::SimClassFieldDeclOp::create(
              builder, getSemanticLocation(property), name, classSymbol,
              builder.getIntegerType(layout->elementCount), ordinal++,
              IntegerAttr{}, /*isStatic=*/false, /*isWeak=*/false,
              builder.getStringAttr("__obelisk_rand_element_modes"));
          sim::set<sim::metadata::RandomArrayModeField>(property, symbol);
          sim::set<sim::metadata::RandomArrayModeField>(field, symbol);
          sim::set<sim::metadata::RandomArrayElementWidth>(
              field, builder.getI32IntegerAttr(layout->elementWidth));
        }
      if (sourceStructure && !sourceStructure.getIsPacked() &&
          !sourceStructure.getIsUnion()) {
        SmallVector<Attribute> members;
        for (auto [index, member] : llvm::enumerate(structMembers)) {
          uint32_t modeIndex =
              property.getRandMode() == semantic::SVRandMode::None
                  ? 0
                  : randomPropertyIndex++;
          auto addField = [&](StringRef suffix, Type fieldType) {
            std::string name =
                fieldName + "_member_" + std::to_string(index) + suffix.str();
            auto symbol = FlatSymbolRefAttr::get(context, name);
            sim::SimClassFieldDeclOp::create(
                builder, getSemanticLocation(property), name, classSymbol,
                fieldType, ordinal++, IntegerAttr{}, /*isStatic=*/false,
                /*isWeak=*/false,
                builder.getStringAttr("__obelisk_random_member_state"));
            return symbol;
          };
          // Static members receive their randc state and element modes once
          // class-wide storage exists.
          sim::RandomCycleStateAttr cycle;
          if (!isStatic && member.isRandC) {
            Type wordState =
                getRandomCyclicStateType(member.type, builder.getI64Type());
            FlatSymbolRefAttr key = addField("_randc_key", wordState);
            FlatSymbolRefAttr position = addField("_randc_position", wordState);
            FlatSymbolRefAttr domain =
                reserveCyclicDomain(getSemanticLocation(property),
                                    fieldName + "_member_" +
                                        std::to_string(index) + "_randc_domain",
                                    member.type);
            cycle =
                sim::RandomCycleStateAttr::get(context, key, position, domain);
          }
          FlatSymbolRefAttr elementModeField;
          if (!isStatic)
            if (auto layout = getFixedRandomArrayLayout(member.type))
              elementModeField =
                  addField("_element_modes",
                           builder.getIntegerType(layout->elementCount));
          members.push_back(sim::RandomStructMemberAttr::get(
              context, builder.getDenseI64ArrayAttr(member.path),
              member.semanticType, member.type, member.isRandC, modeIndex,
              cycle, elementModeField, std::nullopt));
        }
        sim::set<sim::metadata::RandomStructMembers>(
            property, builder.getArrayAttr(members));
      }
    }
    // Every inheritance tree owns exactly one inline PCG stream.
    if (!base && !classType.getIsInterface()) {
      auto addRandomField = [&](StringRef suffix, StringRef debugName) {
        std::string name = (classSymbol.getValue() + "_field_" + suffix).str();
        FlatSymbolRefAttr symbol = FlatSymbolRefAttr::get(context, name);
        sim::SimClassFieldDeclOp::create(
            builder, getSemanticLocation(classType), name, classSymbol,
            builder.getI64Type(), ordinal++, IntegerAttr{},
            /*isStatic=*/false, /*isWeak=*/false,
            builder.getStringAttr(debugName));
        return symbol;
      };
      sim::set<sim::metadata::RandomStateField>(
          declaration,
          addRandomField("__obelisk_rng_state", "__obelisk_rng_state"));
      sim::set<sim::metadata::RandomIncrementField>(
          declaration,
          addRandomField("__obelisk_rng_increment", "__obelisk_rng_increment"));
      sim::set<sim::metadata::RandomModeField>(
          declaration,
          addRandomField("__obelisk_rand_mode", "__obelisk_rand_mode"));
      sim::set<sim::metadata::ConstraintModeField>(
          declaration, addRandomField("__obelisk_constraint_mode",
                                      "__obelisk_constraint_mode"));
    }
    // Append managed histories after the existing inventory so source fields
    // and their ordinal-derived symbols keep their identities.
    for (const auto &[location, name, type] : cyclicDomainFields)
      sim::SimClassFieldDeclOp::create(
          builder, location, name, classSymbol, type, ordinal++, IntegerAttr{},
          /*isStatic=*/false, /*isWeak=*/false,
          builder.getStringAttr("__obelisk_randc_domain"));
  }

  for (const auto &[derived, directBase] : inheritedCovergroupProperties) {
    Operation *storage = directBase;
    llvm::SmallPtrSet<Operation *, 8> visited;
    while (true) {
      auto found = inheritedCovergroupProperties.find(storage);
      if (found == inheritedCovergroupProperties.end())
        break;
      if (!visited.insert(storage).second) {
        emitError(getSemanticLocation(derived))
            << "embedded covergroup property inheritance contains a cycle";
        invalid = true;
        storage = nullptr;
        break;
      }
      storage = found->second;
    }
    if (!storage || !result.fieldSymbols.count(storage)) {
      emitError(getSemanticLocation(derived))
          << "inherited covergroup property has no base storage field";
      invalid = true;
      continue;
    }
    FailureOr<Type> storageType = getNormalizedSemanticType(storage);
    if (failed(storageType)) {
      invalid = true;
      continue;
    }
    result.fieldSymbols[derived] = result.fieldSymbols.lookup(storage);
    result.covergroupFieldStorageTypes[derived] = *storageType;
  }

  // Freeze the effective base-to-derived packed random-variable inventory on
  // each exact class descriptor.  These remain symbolic field references:
  // native and bytecode layouts are deliberately unavailable at preparation.
  // Rand object handles and containers have separate graph/container
  // descriptors and therefore do not masquerade as scalar assignment slots.
  llvm::DenseMap<Operation *, SmallVector<sim::RandomVariableReferenceAttr>>
      randomVariableReferences;
  llvm::SmallPtrSet<Operation *, 8> collectingRandomVariableReferences;
  std::function<LogicalResult(semantic::SVClassTypeOp)>
      collectRandomVariableReferences =
          [&](semantic::SVClassTypeOp classType) -> LogicalResult {
    if (randomVariableReferences.count(classType))
      return success();
    if (!collectingRandomVariableReferences.insert(classType).second)
      return failure();

    SmallVector<sim::RandomVariableReferenceAttr> references;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      if (base == result.semanticClasses.end() ||
          failed(collectRandomVariableReferences(base->second)))
        return failure();
      llvm::append_range(references,
                         randomVariableReferences.find(base->second)->second);
    }
    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      sim::SimClassFieldDeclOp field = fieldDeclarations.lookup(child);
      if (!property || !field ||
          !sim::has<sim::metadata::RandomVariableKind>(field))
        continue;
      references.push_back(sim::RandomVariableReferenceAttr::get(
          context, {}, result.fieldSymbols.lookup(property)));
    }
    collectingRandomVariableReferences.erase(classType);
    randomVariableReferences[classType] = std::move(references);
    return success();
  };
  for (semantic::SVClassTypeOp classType : result.sources) {
    if (failed(collectRandomVariableReferences(classType))) {
      emitError(getSemanticLocation(classType))
          << "cannot determine inherited random-variable references";
      invalid = true;
      continue;
    }
    ArrayRef<sim::RandomVariableReferenceAttr> references =
        randomVariableReferences.find(classType)->second;
    if (!references.empty()) {
      SmallVector<Attribute> attributes = llvm::map_to_vector(
          references, [](sim::RandomVariableReferenceAttr reference) {
            return Attribute(reference);
          });
      result.declarations.lookup(classType).setRandomVariableReferencesAttr(
          builder.getArrayAttr(attributes));
    }
  }

  llvm::DenseMap<Operation *, uint64_t> classVirtualCounts;
  llvm::SmallPtrSet<Operation *, 8> assigningClasses;
  std::function<LogicalResult(semantic::SVClassTypeOp)> assignVirtualSlots =
      [&](semantic::SVClassTypeOp classType) -> LogicalResult {
    if (classVirtualCounts.count(classType))
      return success();
    if (!assigningClasses.insert(classType).second)
      return classType.emitError("class inheritance contains a cycle");
    uint64_t nextSlot = 0;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      if (base == result.semanticClasses.end() ||
          failed(assignVirtualSlots(base->second)))
        return failure();
      nextSlot = classVirtualCounts.lookup(base->second);
    }
    uint64_t methodOrdinal = 0;
    uint64_t interfaceMethodOrdinal = 0;
    for (Operation *child : getChildren(classType)) {
      auto method = getClassMethod(child);
      if (!method || method.getIsBuiltin().value_or(false))
        continue;
      std::string methodName = (result.symbols.lookup(classType).getValue() +
                                "_method_" + llvm::Twine(methodOrdinal++))
                                   .str();
      result.methodSymbols[method] =
          FlatSymbolRefAttr::get(context, methodName);
      if (!method.getIsVirtual().value_or(false))
        continue;
      if (classType.getIsInterface()) {
        result.virtualMethodSlots[method] = UINT32_MAX;
        result.virtualMethodSignatures[method] =
            getVirtualMethodSignatureID(method);
        result.interfaceMethodOrdinals[method] = interfaceMethodOrdinal++;
        continue;
      }
      std::optional<SymbolRefAttr> overridden = method.getOverrideSymbol();
      // For extern/out-of-block class methods, Slang attaches the override
      // relationship to the method prototype while getClassMethod() returns
      // its nested executable subroutine. Preserve the inherited vtable slot
      // instead of accidentally appending a second, concrete slot and leaving
      // the pure base entry effective.
      if (!overridden)
        if (auto prototype =
                dyn_cast<semantic::SVMethodPrototypeSymbolOp>(child))
          overridden = prototype.getOverrideSymbol();
      if (overridden) {
        auto target = semanticSymbols.find(overridden->getLeafReference());
        if (target == semanticSymbols.end() ||
            !result.virtualMethodSlots.count(target->second)) {
          method.emitError(
              "virtual override does not resolve to an inherited slot");
          return failure();
        }
        uint64_t inheritedSlot =
            result.virtualMethodSlots.lookup(target->second);
        result.virtualMethodSlots[method] =
            inheritedSlot == UINT32_MAX ? nextSlot++ : inheritedSlot;
        result.virtualMethodSignatures[method] =
            result.virtualMethodSignatures.lookup(target->second);
      } else {
        result.virtualMethodSlots[method] = nextSlot++;
        result.virtualMethodSignatures[method] =
            getVirtualMethodSignatureID(method);
      }
      nextSlot =
          std::max(nextSlot, result.virtualMethodSlots.lookup(method) + 1);
    }
    assigningClasses.erase(classType);
    classVirtualCounts[classType] = nextSlot;
    return success();
  };
  for (semantic::SVClassTypeOp classType : result.sources)
    if (failed(assignVirtualSlots(classType)))
      invalid = true;

  SymbolTable classTable(design);
  for (semantic::SVClassTypeOp classType : result.sources) {
    auto semanticClassType =
        cast<semantic::ClassHandleType>(classType.getSemanticType());
    StringAttr classSymbol =
        getSimulationClassSymbol(semanticClassType.getClassName());
    if (!classTable.lookup(classSymbol)) {
      emitError(getSemanticLocation(classType))
          << "internal error: flattened class symbol was not inserted";
      invalid = true;
    }
  }
  if (invalid)
    return failure();
  return result;
}

uint64_t PreparedScopeDeclarations::lookup(Operation *operation) const {
  for (Operation *cursor = operation; cursor; cursor = cursor->getParentOp())
    if (auto found = ids.find(cursor); found != ids.end())
      return found->second;
  return 0;
}

uint64_t PreparedScopeDeclarations::lookupInitializationRandomScope(
    Operation *operation) const {
  for (Operation *cursor = operation; cursor; cursor = cursor->getParentOp())
    if (auto found = initializationRandomIds.find(cursor);
        found != initializationRandomIds.end())
      return found->second;
  return 1;
}

FailureOr<PreparedScopeDeclarations> materializeScopeDeclarations(
    semantic::SVRootSymbolOp semanticRoot, ArrayRef<Operation *> units,
    uint64_t designPrecisionFemtoseconds, OpBuilder &builder,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  PreparedScopeDeclarations result;
  // RNG ownership follows lexical semantic identity. Physical scope IDs
  // collapse packages and compilation units into the root, so they cannot
  // distinguish all IEEE 18.14.1 initialization streams.
  result.initializationRandomIds[semanticRoot] = 1;
  uint64_t nextInitializationRandomId = 2;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *operation) {
    if (isa<semantic::SVPackageSymbolOp, semantic::SVCompilationUnitSymbolOp>(
            operation) ||
        (isa<semantic::SVInstanceBodySymbolOp>(operation) &&
         !isCompileTimeOnlyInstanceMember(operation)))
      result.initializationRandomIds[operation] = nextInitializationRandomId++;
  });
  bool invalid = false;
  uint64_t nextScopeId = 0;
  result.ids[semanticRoot] = nextScopeId;
  result.declarations.push_back(sim::SimScopeDeclOp::create(
      builder, getSemanticLocation(semanticRoot), nextScopeId++, IntegerAttr{},
      builder.getStringAttr(getHierarchyName(semanticRoot)),
      builder.getStringAttr(getDebugName(semanticRoot)), StringAttr{},
      IntegerAttr{}, StringAttr{}, IntegerAttr{}, FlatSymbolRefAttr{},
      FlatSymbolRefAttr{}));
  llvm::DenseMap<Operation *, std::pair<FlatSymbolRefAttr, StringAttr>>
      definitionSymbols;
  llvm::StringMap<semantic::SVDefinitionSymbolOp> definitionsBySymbol;
  if (ModuleOp module = semanticRoot->getParentOfType<ModuleOp>())
    module.walk([&](semantic::SVDefinitionSymbolOp definition) {
      definitionsBySymbol.try_emplace(definition.getSymName(), definition);
    });
  uint64_t nextDefinitionId = 0;
  auto materializeDefinition =
      [&](semantic::SVInstanceBodySymbolOp body) -> FlatSymbolRefAttr {
    auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(body->getParentOp());
    if (!instance)
      return {};
    SymbolRefAttr reference = instance.getReferencedSymbolAttr();
    if (!reference)
      return {};
    semantic::SVDefinitionSymbolOp definition =
        SymbolTable::lookupNearestSymbolFrom<semantic::SVDefinitionSymbolOp>(
            instance, reference);
    if (!definition) {
      auto found = definitionsBySymbol.find(reference.getLeafReference());
      if (found != definitionsBySymbol.end())
        definition = found->second;
    }
    if (!definition)
      return {};
    StringAttr definitionName =
        sim::get<sim::metadata::VpiDefinitionName>(body);
    if (!definitionName)
      definitionName = definition->getAttrOfType<StringAttr>("name");
    if (!definitionName)
      definitionName = definition.getSymNameAttr();
    auto existing = definitionSymbols.find(definition);
    if (existing != definitionSymbols.end()) {
      if (existing->second.second != definitionName) {
        emitError(getSemanticLocation(body))
            << "instances of one source definition disagree on the VPI "
               "definition name";
        invalid = true;
      }
      return existing->second.first;
    }

    uint32_t vpiKind = 0;
    switch (definition.getDefinitionKind()) {
    case semantic::SVDefinitionKind::Module:
      vpiKind =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Module);
      break;
    case semantic::SVDefinitionKind::Interface:
      vpiKind =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Interface);
      break;
    case semantic::SVDefinitionKind::Program:
      vpiKind =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Program);
      break;
    }
    std::string symbol =
        (Twine("__obelisk_vpi_definition_") + Twine(nextDefinitionId++)).str();
    Location location = getSemanticLocation(definition);
    LocationAttr definitionLoc;
    if (auto file = location->findInstanceOf<FileLineColLoc>())
      definitionLoc = file;
    sim::SimVPIDefinitionDeclOp::create(builder, location, symbol, vpiKind,
                                        definitionName.getValue(),
                                        definitionLoc);
    FlatSymbolRefAttr result =
        FlatSymbolRefAttr::get(builder.getContext(), symbol);
    definitionSymbols[definition] = {result, definitionName};
    return result;
  };
  semanticRoot->walk<WalkOrder::PreOrder>([&](semantic::SVInstanceBodySymbolOp
                                                  body) {
    Operation *parent = body->getParentOp();
    while (parent && !result.ids.count(parent))
      parent = parent->getParentOp();
    uint64_t parentId = parent ? result.ids.lookup(parent) : 0;
    if (isCompileTimeOnlyInstanceMember(body)) {
      result.ids[body] = parentId;
      return;
    }
    uint64_t id = nextScopeId++;
    result.ids[body] = id;
    StringAttr interfaceType;
    if (auto identity =
            body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity")) {
      std::string key;
      llvm::raw_string_ostream stream(key);
      stream << identity;
      interfaceType = builder.getStringAttr(key);
    }
    StringAttr definitionName;
    auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(body->getParentOp());
    if (instance && instance.getReferencedSymbolAttr()) {
      auto found = semanticSymbols.find(
          instance.getReferencedSymbolAttr().getLeafReference());
      auto definition =
          found == semanticSymbols.end()
              ? semantic::SVDefinitionSymbolOp{}
              : dyn_cast<semantic::SVDefinitionSymbolOp>(found->second);
      if (!definition) {
        emitError(getSemanticLocation(instance))
            << "elaborated instance references an unknown definition";
        invalid = true;
      } else if (definition.getDefinitionKind() ==
                 semantic::SVDefinitionKind::Module) {
        std::optional<StringRef> canonicalName = instance.getReferencedPath();
        if (!canonicalName || canonicalName->empty()) {
          emitError(getSemanticLocation(instance))
              << "module instance has no canonical definition name";
          invalid = true;
        } else {
          definitionName = builder.getStringAttr(*canonicalName);
        }
      }
    }
    sim::SimScopeDeclOp declaration = sim::SimScopeDeclOp::create(
        builder, getSemanticLocation(body), id,
        builder.getI64IntegerAttr(parentId),
        builder.getStringAttr(getHierarchyName(body)),
        builder.getStringAttr(getDebugName(body)), definitionName,
        IntegerAttr{}, interfaceType,
        body->getAttrOfType<IntegerAttr>("vpi_scope_kind"),
        materializeDefinition(body), FlatSymbolRefAttr{});
    if (interfaceType) {
      auto parentBody =
          instance
              ? instance->getParentOfType<semantic::SVInstanceBodySymbolOp>()
              : semantic::SVInstanceBodySymbolOp{};
      if (parentBody && parentBody->hasAttr("virtual_interface_identity"))
        sim::set<sim::metadata::VirtualInterfaceParentMember>(
            declaration, builder.getStringAttr(getDebugName(instance)));
    }
    result.declarations.push_back(declaration);
    auto unitAttr = body->getAttrOfType<IntegerAttr>("time_unit_fs");
    auto precisionAttr = body->getAttrOfType<IntegerAttr>("time_precision_fs");
    if (bool(unitAttr) != bool(precisionAttr)) {
      emitError(getSemanticLocation(body))
          << "elaborated scope time scale must specify both unit and "
             "precision";
      invalid = true;
      return;
    }
    if (!unitAttr)
      return;
    APInt unit = unitAttr.getValue();
    APInt precision = precisionAttr.getValue();
    if (unit.isNegative() || precision.isNegative() ||
        unit.getActiveBits() > 64 || precision.getActiveBits() > 64) {
      emitError(getSemanticLocation(body))
          << "elaborated scope time scale does not fit an unsigned "
             "64-bit value";
      invalid = true;
      return;
    }
    uint64_t unitFs = unit.getZExtValue();
    uint64_t precisionFs = precision.getZExtValue();
    if (unitFs == 0 || precisionFs == 0 || unitFs < precisionFs ||
        unitFs % precisionFs != 0) {
      emitError(getSemanticLocation(body))
          << "invalid elaborated scope time scale " << unitFs << "fs/"
          << precisionFs << "fs";
      invalid = true;
      return;
    }
    declaration->setAttr("dpi_unit_femtoseconds",
                         builder.getI64IntegerAttr(unitFs));
    declaration->setAttr("dpi_precision_femtoseconds",
                         builder.getI64IntegerAttr(precisionFs));
  });

  for (Operation *unit : units) {
    uint64_t scopeID = result.lookup(unit);
    if (scopeID >= result.declarations.size())
      continue;
    auto unitAttr = unit->getAttrOfType<IntegerAttr>("time_unit_fs");
    auto precisionAttr = unit->getAttrOfType<IntegerAttr>("time_precision_fs");
    if (bool(unitAttr) != bool(precisionAttr)) {
      emitError(getSemanticLocation(unit))
          << "elaborated time scale must specify both unit and precision";
      invalid = true;
      continue;
    }
    // Synthetic units inherit their containing scope's time scale. Leaving
    // both fields unset here lets a real elaborated declaration establish it,
    // independent of traversal order.
    if (!unitAttr && !precisionAttr)
      continue;
    uint64_t unitFs = unitAttr.getValue().getZExtValue();
    uint64_t precisionFs = precisionAttr.getValue().getZExtValue();
    sim::SimScopeDeclOp declaration = result.declarations[scopeID];
    if (auto existing =
            declaration->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds");
        existing && existing.getValue().getZExtValue() != unitFs) {
      emitError(getSemanticLocation(unit))
          << "simulation scope has inconsistent time units";
      invalid = true;
      continue;
    }
    if (auto existing = declaration->getAttrOfType<IntegerAttr>(
            "dpi_precision_femtoseconds");
        existing && existing.getValue().getZExtValue() != precisionFs) {
      emitError(getSemanticLocation(unit))
          << "simulation scope has inconsistent time precisions";
      invalid = true;
      continue;
    }
    declaration->setAttr("dpi_unit_femtoseconds",
                         builder.getI64IntegerAttr(unitFs));
    declaration->setAttr("dpi_precision_femtoseconds",
                         builder.getI64IntegerAttr(precisionFs));
  }
  for (sim::SimScopeDeclOp declaration : result.declarations) {
    sim::SimScopeDeclOp parent;
    if (auto parentID = declaration.getParent();
        parentID && *parentID < result.declarations.size())
      parent = result.declarations[*parentID];
    auto inherited = [&](StringRef name) -> IntegerAttr {
      if (parent)
        if (auto value = parent->getAttrOfType<IntegerAttr>(name))
          return value;
      return builder.getI64IntegerAttr(designPrecisionFemtoseconds);
    };
    if (!declaration->hasAttr("dpi_unit_femtoseconds"))
      declaration->setAttr("dpi_unit_femtoseconds",
                           inherited("dpi_unit_femtoseconds"));
    if (!declaration->hasAttr("dpi_precision_femtoseconds"))
      declaration->setAttr("dpi_precision_femtoseconds",
                           inherited("dpi_precision_femtoseconds"));
  }
  if (invalid)
    return failure();
  return result;
}

} // namespace obelisk::simlowering
