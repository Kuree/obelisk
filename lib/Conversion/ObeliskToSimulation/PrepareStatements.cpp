//===- PrepareStatements.cpp - Procedural statement inventory -----------===//

#include "PrepareStatements.h"
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
};

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
    bool sequential =
        block.getBlockKind() == semantic::SVStatementBlockKind::Sequential;
    shape.kind = sequential ? Kind::Begin : Kind::Fork;
    if (auto symbol = block.getBlockSymbolAttr()) {
      // Preparation has validated every elaborated path component. Semantic
      // scopes are isolated, so resolve the exact frozen symbol identity.
      auto found = semanticSymbols.find(symbol.getLeafReference());
      Operation *declaration =
          found == semanticSymbols.end() ? nullptr : found->second;
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
    // Slang's do-while visits its body before its condition. Other supported
    // loops place the body last, after conditions/initializers/increments.
    shape.children.push_back({isa<semantic::SVDoWhileLoopStatementOp>(source)
                                  ? children.front()
                                  : children.back()});
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
} // namespace obelisk::simlowering
