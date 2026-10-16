//===- PrepareStatements.cpp - Procedural statement inventory -----------===//

#include "PrepareStatements.h"
#include "PrepareTopology.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "llvm/ADT/TypeSwitch.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk::simlowering {
namespace {
using Kind = reflection::VPIObjectKind;
using Relation = reflection::VPIRelationKind;
struct Child {
  Operation *source;
  Relation relation = Relation::StmtRel;
  // A case-item grouping and a repeated-event timing wrapper have exact
  // source occurrences but no standalone AST statement operation.
  std::optional<Kind> grouping = std::nullopt;
  Operation *body = nullptr;
};
struct Shape {
  Kind kind;
  StringAttr name;
  SmallVector<Child> children;
  bool isScope = false;
  SmallVector<Attribute> properties;
  Operation *loopAlias = nullptr;
};

static void addIntegerProperty(Shape &shape, Operation *source,
                               uint32_t selector, uint32_t value) {
  Builder builder(source->getContext());
  shape.properties.push_back(sim::VPIPropertyAttr::get(
      builder.getContext(), builder.getI32IntegerAttr(selector),
      builder.getI32IntegerAttr(value)));
}

static uint32_t qualifier(semantic::SVUniquePriorityCheck check) {
  switch (check) {
  case semantic::SVUniquePriorityCheck::None:
    return 0;
  case semantic::SVUniquePriorityCheck::Unique:
  case semantic::SVUniquePriorityCheck::Unique0:
    // Annex M represents the uniqueness qualifier with one bit; it has no
    // separate unique0 value.
    return 1;
  case semantic::SVUniquePriorityCheck::Priority:
    return 2;
  }
  llvm_unreachable("unknown source qualifier");
}

static void addCaseProperties(Shape &shape, Operation *source,
                              semantic::SVCaseCondition condition,
                              semantic::SVUniquePriorityCheck check,
                              bool pattern = false) {
  uint32_t caseType = 1; // vpiCaseExact
  if (condition == semantic::SVCaseCondition::WildcardXOrZ)
    caseType = 2; // vpiCaseX
  else if (condition == semantic::SVCaseCondition::WildcardJustZ)
    caseType = 3; // vpiCaseZ
  addIntegerProperty(shape, source, 42, caseType);
  uint32_t flags = qualifier(check);
  if (condition == semantic::SVCaseCondition::Inside)
    flags |= 16; // vpiInsideQualifier
  if (pattern)
    flags |= 4; // vpiTaggedQualifier
  addIntegerProperty(shape, source, 650, flags);
}

// Explicit receivers and exact declaration ownership both establish a method.
// Static methods and implicit-this calls need not carry a receiver expression.
static bool isMethodCall(semantic::SVCallExpressionOp call,
                         const llvm::StringMap<Operation *> &semanticSymbols) {
  if (call.getHasThisClass() || call->hasAttr("is_builtin_method"))
    return true;
  auto symbol = call.getReferencedSymbolAttr();
  if (!symbol)
    return false;
  auto found = semanticSymbols.find(symbol.getLeafReference());
  if (found == semanticSymbols.end())
    return false;
  Operation *declaration = found->second;
  if (!isa<semantic::SVSubroutineSymbolOp, semantic::SVMethodPrototypeSymbolOp>(
          declaration))
    return false;
  if (declaration->getParentOfType<semantic::SVClassTypeOp>())
    return true;
  // An out-of-block implementation has an exact prototype reference to the
  // class-owned declaration; its lexical implementation owner is insufficient.
  auto prototype =
      declaration->getAttrOfType<SymbolRefAttr>("prototype_symbol");
  if (!prototype)
    return false;
  auto owner = semanticSymbols.find(prototype.getLeafReference());
  return owner != semanticSymbols.end() &&
         isa<semantic::SVMethodPrototypeSymbolOp>(owner->second) &&
         owner->second->getParentOfType<semantic::SVClassTypeOp>();
}

// Only actual statement occurrences enter this inventory. In particular an
// expression used as a statement does not become a task call just because
// its result is discarded, and declaration initialization is not an invented
// assignment statement.
static std::optional<Shape>
statementShape(Operation *source,
               const llvm::StringMap<Operation *> &semanticSymbols) {
  SmallVector<Operation *> children = getChildren(source);
  Shape shape{Kind::NullStmt, {}, {}};
  if (isa<semantic::SVStatementListOp>(source)) {
    // IEEE 1800-2023 37.12 detail 5 requires this unnamed begin for a
    // task/function containing more than one statement. The caller removes
    // zero/one-statement task lists before reaching this case.
    shape.kind = Kind::Begin;
    for (Operation *child : children)
      if (!isa<semantic::SVVariableDeclStatementOp>(child))
        shape.children.push_back({child});
    return shape;
  }
  if (auto block = dyn_cast<semantic::SVBlockStatementOp>(source)) {
    if (block.getIsImplicitLoopScope()) {
      if (children.size() == 1 &&
          isa<semantic::SVStatementListOp>(children.front()))
        children = getChildren(children.front());
      auto result = statementShape(children.back(), semanticSymbols);
      if (!result)
        return std::nullopt;
      SymbolTableCollection symbols;
      auto module = block->getParentOfType<ModuleOp>();
      auto *declaration =
          module ? symbols.lookupSymbolIn(module, block.getBlockSymbolAttr())
                 : nullptr;
      if (!declaration)
        return std::nullopt;
      if (auto name = declaration->getAttrOfType<StringAttr>("name");
          name && !name.empty())
        result->name = name;
      result->loopAlias = children.back();
      return result;
    }
    bool sequential =
        block.getBlockKind() == semantic::SVStatementBlockKind::Sequential;
    shape.kind = sequential ? Kind::Begin : Kind::Fork;
    if (auto symbol = block.getBlockSymbolAttr()) {
      // Distinct unnamed blocks may reuse the same leaf spelling. Resolve the
      // exact source symbol path in its module namespace.
      SymbolTableCollection symbols;
      auto module = block->getParentOfType<ModuleOp>();
      Operation *declaration =
          module ? symbols.lookupSymbolIn(module, symbol) : nullptr;
      auto name = declaration ? declaration->getAttrOfType<StringAttr>("name")
                              : StringAttr{};
      if (!declaration)
        return std::nullopt;
      if (name && !name.empty()) {
        shape.kind = sequential ? Kind::NamedBegin : Kind::NamedFork;
        shape.name = name;
        shape.isScope = true;
      }
      for (Operation *item : getChildren(declaration))
        shape.isScope |=
            isa<semantic::SVVariableSymbolOp, semantic::SVTypeAliasTypeOp,
                semantic::SVParameterSymbolOp,
                semantic::SVTypeParameterSymbolOp, semantic::SVNetTypeOp,
                semantic::SVExplicitImportSymbolOp,
                semantic::SVWildcardImportSymbolOp>(item);
    }
    if (children.size() == 1 && isa<semantic::SVStatementListOp>(children[0]))
      children = getChildren(children[0]);
    for (Operation *child : children) {
      if (isa<semantic::SVVariableDeclStatementOp>(child))
        shape.isScope = true;
      else
        shape.children.push_back({child});
    }
    return shape;
  }
  if (auto conditional = dyn_cast<semantic::SVConditionalStatementOp>(source)) {
    size_t count = 1 + conditional.getHasElse();
    if (children.size() < count)
      return std::nullopt;
    shape.kind = conditional.getHasElse() ? Kind::IfElse : Kind::If;
    addIntegerProperty(shape, source, 650,
                       qualifier(conditional.getCheckKind()));
    shape.children.push_back({children[children.size() - count]});
    if (conditional.getHasElse())
      shape.children.push_back({children.back(), Relation::ElseStmtRel});
    return shape;
  }
  if (auto statement = dyn_cast<semantic::SVRandCaseStatementOp>(source)) {
    // 18.16 and 37.72: randcase is a case statement with the rand qualifier.
    // Each source weight/body pair owns a case item; there is no controlling
    // condition expression and the weight remains unavailable until imported.
    shape.kind = Kind::Case;
    if (children.size() != 2 * statement.getItemCount())
      return std::nullopt;
    Builder builder(source->getContext());
    shape.properties.push_back(sim::VPIPropertyAttr::get(
        builder.getContext(), builder.getI32IntegerAttr(650),
        builder.getI32IntegerAttr(8))); // vpiQualifier = vpiRandQualifier.
    // The semantic importer visits all weights before the corresponding bodies.
    for (size_t i = statement.getItemCount(); i < children.size(); ++i) {
      Operation *body = children[i];
      shape.children.push_back(
          {body, static_cast<Relation>(Kind::CaseItem), Kind::CaseItem, body});
    }
    return shape;
  }
  if (auto statement = dyn_cast<semantic::SVCaseStatementOp>(source)) {
    shape.kind = Kind::Case;
    addCaseProperties(shape, source, statement.getConditionKind(),
                      statement.getCheckKind());
    size_t next = 1; // The selector is an expression, not a statement.
    for (int64_t count : statement.getItemLabelCounts()) {
      next += count;
      if (next >= children.size())
        return std::nullopt;
      Operation *body = children[next++];
      shape.children.push_back(
          {body, static_cast<Relation>(Kind::CaseItem), Kind::CaseItem, body});
    }
    if (statement.getHasDefault()) {
      if (next >= children.size())
        return std::nullopt;
      Operation *body = children[next++];
      shape.children.push_back(
          {body, static_cast<Relation>(Kind::CaseItem), Kind::CaseItem, body});
    }
    return next == children.size() ? std::optional(shape) : std::nullopt;
  }
  if (auto statement = dyn_cast<semantic::SVPatternCaseStatementOp>(source)) {
    shape.kind = Kind::Case;
    addCaseProperties(shape, source, statement.getConditionKind(),
                      statement.getCheckKind(), true);
    size_t next = 1;
    for (int64_t hasFilter : statement.getItemFilterFlags()) {
      next += 1 + hasFilter; // Pattern and optional filter remain expressions.
      if (next >= children.size())
        return std::nullopt;
      Operation *body = children[next++];
      shape.children.push_back(
          {body, static_cast<Relation>(Kind::CaseItem), Kind::CaseItem, body});
    }
    if (statement.getHasDefault()) {
      if (next >= children.size())
        return std::nullopt;
      Operation *body = children[next++];
      shape.children.push_back(
          {body, static_cast<Relation>(Kind::CaseItem), Kind::CaseItem, body});
    }
    return next == children.size() ? std::optional(shape) : std::nullopt;
  }
  if (auto statement = dyn_cast<semantic::SVForLoopStatementOp>(source)) {
    shape.kind = Kind::For;
    shape.isScope = statement.getHasLocalVariables();
    size_t next = 0;
    for (uint64_t i = 0; i < statement.getInitializerCount(); ++i)
      shape.children.push_back({children[next++], Relation::ForInitStmtRel});
    next += statement.getHasCondition();
    for (uint64_t i = 0; i < statement.getStepCount(); ++i)
      shape.children.push_back({children[next++], Relation::ForIncStmtRel});
    shape.children.push_back({children[next]});
    return shape;
  }
  if (auto statement =
          dyn_cast<semantic::SVImmediateAssertionStatementOp>(source)) {
    switch (statement.getAssertionKind()) {
    case semantic::SVAssertionKind::Assert:
      shape.kind = Kind::ImmediateAssert;
      break;
    case semantic::SVAssertionKind::Assume:
      shape.kind = Kind::ImmediateAssume;
      break;
    case semantic::SVAssertionKind::CoverProperty:
      shape.kind = Kind::ImmediateCover;
      break;
    default:
      return std::nullopt;
    }
    size_t next = 1;
    if (next + statement.getHasPassAction() + statement.getHasFailAction() !=
        children.size())
      return std::nullopt;
    if (statement.getHasPassAction())
      shape.children.push_back({children[next++]});
    if (statement.getHasFailAction())
      shape.children.push_back({children[next], Relation::ElseStmtRel});
    return shape;
  }
  if (auto statement =
          dyn_cast<semantic::SVConcurrentAssertionStatementOp>(source);
      statement &&
      statement.getAssertionKind() == semantic::SVAssertionKind::Expect) {
    shape.kind = Kind::ExpectStmt;
    // Assertion expression and optional disable are not statement children.
    size_t count = statement.getHasPassAction() + statement.getHasFailAction();
    if (children.size() < count + 1)
      return std::nullopt;
    size_t next = children.size() - count;
    if (statement.getHasPassAction())
      shape.children.push_back({children[next++]});
    if (statement.getHasFailAction())
      shape.children.push_back({children[next], Relation::ElseStmtRel});
    return shape;
  }
  if (auto statement =
          dyn_cast<semantic::SVProceduralAssignStatementOp>(source)) {
    shape.kind = statement.getIsForce() ? Kind::Force : Kind::AssignStmt;
    return shape;
  }
  if (auto statement =
          dyn_cast<semantic::SVProceduralDeassignStatementOp>(source)) {
    shape.kind = statement.getIsRelease() ? Kind::Release : Kind::Deassign;
    return shape;
  }
  if (isa<semantic::SVTimedStatementOp>(source)) {
    if (children.size() != 2)
      return std::nullopt;
    if (isa<semantic::SVRepeatedEventControlOp>(children[0])) {
      auto controls = getChildren(children[0]);
      if (controls.size() != 2 ||
          !isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp,
               semantic::SVImplicitEventControlOp>(controls[1]))
        return std::nullopt;
      shape.kind = Kind::RepeatControl;
      shape.children.push_back({controls[1],
                                static_cast<Relation>(Kind::EventControl),
                                Kind::EventControl, children[1]});
      return shape;
    }
    if (isa<semantic::SVDelayControlOp, semantic::SVOneStepDelayControlOp>(
            children[0]))
      shape.kind = Kind::DelayControl;
    else if (isa<semantic::SVSignalEventControlOp,
                 semantic::SVEventListControlOp,
                 semantic::SVImplicitEventControlOp>(children[0]))
      shape.kind = Kind::EventControl;
    else
      return std::nullopt;
    shape.children.push_back({children[1]});
    return shape;
  }
  auto loop =
      llvm::TypeSwitch<Operation *, std::optional<Kind>>(source)
          .Case<semantic::SVWhileLoopStatementOp>(
              [](auto) { return Kind::While; })
          .Case<semantic::SVDoWhileLoopStatementOp>(
              [](auto) { return Kind::DoWhile; })
          .Case<semantic::SVRepeatLoopStatementOp>(
              [](auto) { return Kind::Repeat; })
          .Case<semantic::SVForeachLoopStatementOp>(
              [](auto) { return Kind::ForeachStmt; })
          .Case<semantic::SVForeverLoopStatementOp>(
              [](auto) { return Kind::Forever; })
          .Case<semantic::SVWaitStatementOp>([](auto) { return Kind::Wait; })
          .Default(std::nullopt);
  if (loop) {
    if (children.empty())
      return std::nullopt;
    shape.kind = *loop;
    // The semantic inventory visits loop conditions before the body, including
    // do-while (whose source spelling places the body first).
    shape.children.push_back({children.back()});
    return shape;
  }
  if (auto wait = dyn_cast<semantic::SVWaitOrderStatementOp>(source)) {
    shape.kind = Kind::OrderedWait;
    size_t next = wait.getEventCount();
    if (next + wait.getHasSuccessAction() + wait.getHasFailureAction() !=
        children.size())
      return std::nullopt;
    if (wait.getHasSuccessAction())
      shape.children.push_back({children[next++]});
    if (wait.getHasFailureAction())
      shape.children.push_back({children[next], Relation::ElseStmtRel});
    return shape;
  }
  if (isa<semantic::SVDelayControlOp, semantic::SVOneStepDelayControlOp>(
          source)) {
    shape.kind = Kind::DelayControl;
    return shape;
  }
  if (isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp,
          semantic::SVImplicitEventControlOp>(source)) {
    shape.kind = Kind::EventControl;
    return shape;
  }
  if (isa<semantic::SVRepeatedEventControlOp>(source)) {
    if (children.size() != 2)
      return std::nullopt;
    shape.kind = Kind::RepeatControl;
    shape.children.push_back(
        {children[1], static_cast<Relation>(Kind::EventControl)});
    return shape;
  }
  if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(source)) {
    shape.kind = Kind::Assignment;
    uint32_t operatorType = 82; // vpiAssignmentOp, including nonblocking <=.
    if (auto binary = assignment.getOperatorKind()) {
      auto name = semantic::stringifySVBinaryOperator(*binary);
      auto *operation = reflection::findVPISourceOperator(
          std::string_view(name.data(), name.size()), 2);
      if (!operation)
        return std::nullopt;
      operatorType = operation->value;
    }
    addIntegerProperty(shape, source, 39, operatorType);
    Builder builder(source->getContext());
    shape.properties.push_back(sim::VPIPropertyAttr::get(
        builder.getContext(), builder.getI32IntegerAttr(41),
        builder.getBoolAttr(assignment.getAssignmentKind() ==
                            semantic::SVAssignmentKind::Blocking)));
    if (assignment.getHasTimingControl()) {
      if (children.size() != 3)
        return std::nullopt;
      std::optional<Kind> timingKind;
      if (isa<semantic::SVDelayControlOp, semantic::SVOneStepDelayControlOp>(
              children[0]))
        timingKind = Kind::DelayControl;
      else if (isa<semantic::SVSignalEventControlOp,
                   semantic::SVEventListControlOp,
                   semantic::SVImplicitEventControlOp>(children[0]))
        timingKind = Kind::EventControl;
      else if (isa<semantic::SVRepeatedEventControlOp>(children[0]))
        timingKind = Kind::RepeatControl;
      if (timingKind)
        shape.children.push_back(
            {children[0], static_cast<Relation>(*timingKind)});
    }
    return shape;
  }
  if (isa<semantic::SVExpressionStatementOp>(source)) {
    if (children.size() != 1)
      return std::nullopt;
    if (isa<semantic::SVAssignmentExpressionOp>(children[0]))
      return statementShape(children[0], semanticSymbols);
    // A void cast around a call discards its result; the atomic statement
    // remains the exact tf-call kind (37.60, 37.42).
    Operation *expression = children[0];
    if (auto cast = dyn_cast<semantic::SVConversionExpressionOp>(expression);
        cast && cast.getSemanticType() &&
        isa<semantic::VoidType>(cast.getSemanticType())) {
      auto operand = getChildren(cast);
      if (operand.size() != 1)
        return std::nullopt;
      expression = operand[0];
    }
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(expression)) {
      bool task = call.getSubroutineKind() == semantic::SVSubroutineKind::Task;
      shape.kind = isMethodCall(call, semanticSymbols)
                       ? (task ? Kind::MethodTaskCall : Kind::MethodFuncCall)
                   : call.getIsSystemCall()
                       ? (task ? Kind::SysTaskCall : Kind::SysFuncCall)
                       : (task ? Kind::TaskCall : Kind::FuncCall);
    } else
      return std::nullopt;
    return shape;
  }
  auto leaf =
      llvm::TypeSwitch<Operation *, std::optional<Kind>>(source)
          .Case<semantic::SVEmptyStatementOp>(
              [](auto) { return Kind::NullStmt; })
          .Case<semantic::SVReturnStatementOp>(
              [](auto) { return Kind::ReturnStmt; })
          .Case<semantic::SVBreakStatementOp>([](auto) { return Kind::Break; })
          .Case<semantic::SVContinueStatementOp>(
              [](auto) { return Kind::Continue; })
          .Case<semantic::SVDisableStatementOp>(
              [](auto) { return Kind::Disable; })
          .Case<semantic::SVDisableForkStatementOp>(
              [](auto) { return Kind::DisableFork; })
          .Case<semantic::SVWaitForkStatementOp>(
              [](auto) { return Kind::WaitFork; })
          .Case<semantic::SVEventTriggerStatementOp>(
              [](auto) { return Kind::EventStmt; })
          .Default(std::nullopt);
  if (!leaf)
    return std::nullopt;
  shape.kind = *leaf;
  return shape;
}

struct Owner {
  sim::VPIStatementSourceKind category;
  uint64_t id;
  Kind kind;
};
} // namespace

LogicalResult materializeProceduralStatements(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<Operation *> &semanticSymbols, OpBuilder &builder) {
  uint64_t nextID = 1;
  for (auto existing :
       design.getBody().front().getOps<sim::SimStatementDeclOp>())
    nextID = std::max(nextID, static_cast<uint64_t>(existing.getId()) + 1);
  uint64_t nextSiteID = 1;
  for (auto existing :
       design.getBody().front().getOps<sim::SimStatementSiteDeclOp>())
    nextSiteID =
        std::max(nextSiteID, static_cast<uint64_t>(existing.getId()) + 1);
  for (const PreparedUnit &unit : units.units) {
    Operation *source = unit.source;
    bool procedure = isa<semantic::SVProceduralBlockSymbolOp>(source);
    bool subroutine = isa<semantic::SVSubroutineSymbolOp>(source);
    if (!procedure && !subroutine)
      continue;
    Kind ownerKind;
    switch (unit.entryKind) {
    case sim::EntryKind::Initial:
      ownerKind = Kind::Initial;
      break;
    case sim::EntryKind::Final:
      ownerKind = Kind::Final;
      break;
    case sim::EntryKind::Always:
    case sim::EntryKind::AlwaysComb:
    case sim::EntryKind::AlwaysLatch:
    case sim::EntryKind::AlwaysFF:
      ownerKind = Kind::Always;
      break;
    case sim::EntryKind::Task:
      ownerKind = Kind::Task;
      break;
    case sim::EntryKind::Function:
      ownerKind = Kind::Function;
      break;
    default:
      continue;
    }
    auto body = getChildren(source);
    if (subroutine)
      llvm::erase_if(
          body, [](Operation *child) { return isa<SymbolOpInterface>(child); });
    // Semantic subroutine bodies contain one real statement or one
    // StatementList. No executable descendant is substituted for an unsupported
    // root form.
    if (body.size() != 1)
      continue;
    if (subroutine && isa<semantic::SVStatementListOp>(body.front())) {
      auto statements = getChildren(body.front());
      llvm::erase_if(statements, [](Operation *child) {
        return isa<semantic::SVVariableDeclStatementOp>(child);
      });
      if (statements.empty())
        continue;
      if (statements.size() == 1)
        body = std::move(statements);
    }
    auto codeUnit = units.declarations.lookup(source);
    const uint64_t scopeID = codeUnit.getScopeId();
    struct Work {
      Operation *source;
      Owner owner;
      Relation relation;
      std::optional<Kind> grouping = std::nullopt;
      Operation *body = nullptr;
    };
    SmallVector<Work> worklist{
        {body.front(),
         {sim::VPIStatementSourceKind::CodeUnit, unit.id, ownerKind},
         Relation::StmtRel}};
    std::map<std::tuple<sim::VPIStatementSourceKind, uint64_t, uint32_t>,
             uint64_t>
        ordinals;
    while (!worklist.empty()) {
      Work work = worklist.pop_back_val();
      auto shape = work.grouping
                       ? std::optional(Shape{*work.grouping, {}, {{work.body}}})
                       : statementShape(work.source, semanticSymbols);
      if (!shape)
        continue;
      const uint32_t sourceKind = static_cast<uint32_t>(work.owner.kind);
      const uint32_t selector = static_cast<uint32_t>(work.relation);
      const uint32_t targetKind = static_cast<uint32_t>(shape->kind);
      uint32_t modes = 0;
      for (uint32_t mode = 0; mode != 2; ++mode)
        if (auto *edge = reflection::findVPITraversal(
                sourceKind, selector,
                static_cast<reflection::VPITraversalMode>(mode));
            edge && edge->statementContainment &&
            reflection::vpiObjectSetContains(edge->targets, targetKind))
          modes |= uint32_t{1} << mode;
      if (!modes)
        continue;
      if (nextID > INT64_MAX)
        return work.source->emitError(
            "VPI statement inventory exceeds its ID domain");
      uint64_t id = nextID++;
      Location location = getSemanticLocation(work.source);
      bool scope = shape->isScope || shape->kind == Kind::ForeachStmt;
      auto declaration = sim::SimStatementDeclOp::create(
          builder, location, id, builder.getI64IntegerAttr(unit.id), scopeID,
          targetKind,
          work.owner.category == sim::VPIStatementSourceKind::Statement
              ? builder.getI64IntegerAttr(work.owner.id)
              : IntegerAttr{},
          shape->name, scope ? builder.getUnitAttr() : UnitAttr{}, UnitAttr{});
      if (!shape->properties.empty())
        declaration->setAttr(
            "vpi_properties",
            sim::VPIPropertySetAttr::get(
                builder.getContext(), builder.getArrayAttr(shape->properties)));
      if (const auto *callback =
              reflection::findVPIStatementCallback(targetKind))
        for (uint32_t phase = 0; phase != 8; ++phase)
          if (callback->phaseMask & (uint8_t{1} << phase)) {
            if (nextSiteID > INT64_MAX)
              return work.source->emitError(
                  "VPI statement callback inventory exceeds its ID domain");
            sim::SimStatementSiteDeclOp::create(builder, location, nextSiteID++,
                                                id, phase);
          }
      uint64_t ordinal =
          ordinals[{work.owner.category, work.owner.id, selector}]++;
      // The legacy singular for-init/inc relation selects ordinal zero;
      // remaining list members are iterate-only (37.74).
      if (ordinal != 0)
        modes &= ~(uint32_t{1} << static_cast<uint32_t>(
                       reflection::VPITraversalMode::Handle));
      sim::SimVPIStatementRelationDeclOp::create(
          builder, location, work.owner.category, work.owner.id, sourceKind,
          selector, ordinal, modes, id);
      if (!work.grouping)
        sim::set<sim::metadata::VpiStatementId>(work.source,
                                                builder.getI64IntegerAttr(id));
      if (shape->loopAlias)
        sim::set<sim::metadata::VpiStatementId>(shape->loopAlias,
                                                builder.getI64IntegerAttr(id));
      if (procedure &&
          work.owner.category == sim::VPIStatementSourceKind::CodeUnit)
        sim::SimVPIThreadOriginBindingOp::create(builder, location, unit.id,
                                                 id);
      Owner childOwner{sim::VPIStatementSourceKind::Statement, id, shape->kind};
      for (const Child &child : llvm::reverse(shape->children))
        worklist.push_back({child.source, childOwner, child.relation,
                            child.grouping, child.body});
    }
  }
  return success();
}

LogicalResult materializeProceduralTypes(ModuleOp module,
                                         sim::SimDesignOp design,
                                         const PreparedProceduralTypes &types,
                                         OpBuilder &builder) {
  llvm::DenseMap<uint64_t, sim::SimStatementDeclOp> statements;
  for (auto statement : design.getOps<sim::SimStatementDeclOp>())
    statements[statement.getId()] = statement;
  llvm::DenseMap<Operation *, sim::SimStatementDeclOp> scopes;
  SymbolTableCollection symbols;
  bool invalid = false;
  module.walk([&](semantic::SVBlockStatementOp block) {
    auto reference = block.getBlockSymbolAttr();
    auto identity = sim::get<sim::metadata::VpiStatementId>(block);
    if (!reference || !identity)
      return;
    auto statement = statements.lookup(identity.getValue().getZExtValue());
    if (!statement || !statement.getIsScope().value_or(false))
      return;
    auto *source = symbols.lookupSymbolIn(module, reference);
    if (!isa_and_nonnull<semantic::SVStatementBlockSymbolOp>(source)) {
      block.emitError("procedural type owner has no exact source block symbol");
      invalid = true;
      return;
    }
    auto [entry, inserted] = scopes.try_emplace(source, statement);
    if (!inserted && entry->second != statement) {
      block.emitError("source block has multiple procedural scope identities");
      invalid = true;
    }
  });
  if (invalid)
    return failure();
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&design.getBody().front());
  llvm::DenseSet<Attribute> emitted;
  for (const auto &plan : types.typespecs) {
    auto owner = scopes.lookup(plan.block);
    // Unsupported statement ancestors have no immutable scope. Preserve no
    // outward substitute; locals using this type remain unavailable too.
    if (!owner)
      continue;
    if (owner.getScopeId() != plan.scope)
      return plan.source->emitError(
          "procedural typespec scope differs from its exact statement owner");
    sim::SimVPITypespecDeclOp::create(
        builder, getSemanticLocation(plan.source), plan.symbol.getValue(),
        plan.id, plan.scope, FlatSymbolRefAttr{}, plan.hierarchy, plan.name,
        plan.type, plan.origin, plan.sourceIdentity, TypeAttr{},
        owner.getIdAttr());
    emitted.insert(plan.symbol);
  }
  for (const auto &plan : types.constants) {
    if (!emitted.contains(plan.typespec))
      continue;
    sim::SimVPIEnumConstDeclOp::create(
        builder, getSemanticLocation(plan.source), plan.id, plan.ordinal,
        plan.typespec.getValue(), plan.name, plan.value, plan.immutableValue);
  }
  // Deferred declarations under unsupported statement owners may be omitted.
  // Their symbols remain distinct, while the retained image inventories must
  // have dense numeric IDs. No executable metadata refers to these numeric IDs.
  SmallVector<Operation *> retainedTypes;
  for (Operation &op : design.getBody().front())
    if (isa<sim::SimVPITypespecDeclOp, sim::SimVPIUnresolvedTypeAliasDeclOp,
            sim::SimVPILexicalClassTypespecDeclOp>(op))
      retainedTypes.push_back(&op);
  llvm::sort(retainedTypes, [](Operation *a, Operation *b) {
    return a->getAttrOfType<IntegerAttr>("id").getInt() <
           b->getAttrOfType<IntegerAttr>("id").getInt();
  });
  for (auto [id, op] : llvm::enumerate(retainedTypes))
    op->setAttr("id", builder.getI64IntegerAttr(id));
  SmallVector<sim::SimVPIEnumConstDeclOp> retainedConstants;
  for (auto constant : design.getOps<sim::SimVPIEnumConstDeclOp>())
    retainedConstants.push_back(constant);
  llvm::sort(retainedConstants,
             [](auto a, auto b) { return a.getId() < b.getId(); });
  for (auto [id, constant] : llvm::enumerate(retainedConstants))
    constant.setIdAttr(builder.getI64IntegerAttr(id));
  return success();
}

LogicalResult
materializeProceduralCalls(sim::SimDesignOp design, const PreparedUnits &units,
                           const llvm::StringMap<Operation *> &semanticSymbols,
                           OpBuilder &builder) {
  uint64_t nextID = 1;
  for (auto existing : design.getBody().front().getOps<sim::SimVPICallDeclOp>())
    nextID = std::max(nextID, static_cast<uint64_t>(existing.getId()) + 1);
  SymbolTable symbols(design);
  llvm::DenseSet<uint64_t> initializerUnits;
  for (auto binding : design.getOps<sim::SimVPIInitializerBindingOp>())
    initializerUnits.insert(binding.getCodeUnitId());
  for (const PreparedUnit &unit : units.units) {
    if (!initializerUnits.contains(unit.id) &&
        !isa<semantic::SVProceduralBlockSymbolOp,
             semantic::SVSubroutineSymbolOp>(unit.source))
      continue;
    auto owner = units.declarations.lookup(unit.source);
    if (!owner)
      continue;
    SmallVector<Operation *> pending{unit.source};
    while (!pending.empty()) {
      Operation *source = pending.pop_back_val();
      // Nested source units, including static local initializers, own their
      // occurrences independently. Automatic declaration initializers remain
      // in this procedural owner because they have no separate helper unit.
      if (source != unit.source && units.declarations.count(source))
        continue;
      auto children = getChildren(source);
      for (Operation *child : llvm::reverse(children))
        pending.push_back(child);
      auto call = dyn_cast<semantic::SVCallExpressionOp>(source);
      if (!call)
        continue;
      if (nextID > INT64_MAX)
        return call.emitOpError(
            "VPI call occurrence inventory exceeds its ID domain");
      bool task = call.getSubroutineKind() == semantic::SVSubroutineKind::Task;
      Kind kind = isMethodCall(call, semanticSymbols)
                      ? (task ? Kind::MethodTaskCall : Kind::MethodFuncCall)
                  : call.getIsSystemCall()
                      ? (task ? Kind::SysTaskCall : Kind::SysFuncCall)
                      : (task ? Kind::TaskCall : Kind::FuncCall);
      IntegerAttr statementID, enclosingID, calleeID;
      for (Operation *parent = call->getParentOp();
           parent && parent != unit.source; parent = parent->getParentOp()) {
        auto id = sim::get<sim::metadata::VpiStatementId>(parent);
        if (!id)
          continue;
        enclosingID = id;
        if (isa<semantic::SVExpressionStatementOp>(parent)) {
          auto children = getChildren(parent);
          Operation *expression = children.size() == 1 ? children[0] : nullptr;
          if (auto cast =
                  dyn_cast_if_present<semantic::SVConversionExpressionOp>(
                      expression);
              cast && cast.getSemanticType() &&
              isa<semantic::VoidType>(cast.getSemanticType())) {
            auto operand = getChildren(cast);
            expression = operand.size() == 1 ? operand[0] : nullptr;
          }
          if (expression == call.getOperation())
            statementID = id;
        }
        break;
      }
      bool isVirtual = !units.resolveVirtualInterfaceCallees(call).empty();
      if (!call.getIsSystemCall())
        if (auto reference = call.getReferencedSymbolAttr()) {
          auto found = semanticSymbols.find(reference.getLeafReference());
          Operation *target =
              found == semanticSymbols.end() ? nullptr : found->second;
          if (auto implementation = units.externCalleeTargets.find(target);
              implementation != units.externCalleeTargets.end())
            target = implementation->second;
          if (auto prototype =
                  dyn_cast_if_present<semantic::SVMethodPrototypeSymbolOp>(
                      target))
            for (Operation *child : getChildren(prototype))
              if (isa<semantic::SVSubroutineSymbolOp>(child)) {
                target = child;
                break;
              }
          if (target && !target->hasAttr("is_builtin")) {
            if (auto callee = units.declarations.lookup(target))
              calleeID = callee.getIdAttr();
            isVirtual |= target->hasAttr("is_virtual") &&
                         !(call.getIsSuperClass() && !call.getHasThisClass());
          }
        }
      auto declaration = sim::SimVPICallDeclOp::create(
          builder, getSemanticLocation(call),
          ("__obelisk_vpi_call_" + Twine(nextID)).str(), nextID, unit.id,
          owner.getScopeId(), static_cast<uint32_t>(kind), call.getCalleeName(),
          statementID, enclosingID, calleeID, isVirtual);
      symbols.insert(declaration);
      sim::set<sim::metadata::VpiCallSite>(
          call, FlatSymbolRefAttr::get(declaration.getSymNameAttr()));
      ++nextID;
    }
  }
  return success();
}
} // namespace obelisk::simlowering
