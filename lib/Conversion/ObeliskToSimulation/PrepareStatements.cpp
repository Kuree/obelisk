//===- PrepareStatements.cpp - Procedural statement inventory --------------===//

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
};
struct Shape {
  Kind kind;
  StringAttr name;
  SmallVector<Child> children;
};

// Only actual statement occurrences enter this inventory. In particular an
// expression used as a statement does not become a task call just because
// its result is discarded, and declaration initialization is not an invented
// assignment statement.
static std::optional<Shape>
statementShape(Operation *source,
               const llvm::StringMap<Operation *> &semanticSymbols) {
  SmallVector<Operation *> children = getChildren(source);
  Shape shape{Kind::NullStmt, {}, {}};
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
      if (!name || name.empty())
        return std::nullopt;
      shape.kind = sequential ? Kind::NamedBegin : Kind::NamedFork;
      shape.name = name;
    }
    if (children.size() == 1 && isa<semantic::SVStatementListOp>(children[0]))
      children = getChildren(children[0]);
    for (Operation *child : children)
      if (!isa<semantic::SVVariableDeclStatementOp>(child))
        shape.children.push_back({child});
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
  if (isa<semantic::SVTimedStatementOp>(source)) {
    if (children.size() != 2)
      return std::nullopt;
    if (isa<semantic::SVDelayControlOp>(children[0]))
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
          .Case<semantic::SVForLoopStatementOp>([](auto) { return Kind::For; })
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
  if (isa<semantic::SVExpressionStatementOp>(source)) {
    if (children.size() != 1)
      return std::nullopt;
    if (isa<semantic::SVAssignmentExpressionOp>(children[0]))
      shape.kind = Kind::Assignment;
    else if (auto call = dyn_cast<semantic::SVCallExpressionOp>(children[0]);
             call &&
             call.getSubroutineKind() == semantic::SVSubroutineKind::Task)
      shape.kind = call.getIsSystemCall()   ? Kind::SysTaskCall
                   : call.getHasThisClass() ? Kind::MethodTaskCall
                                            : Kind::TaskCall;
    else
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
    auto source = dyn_cast<semantic::SVProceduralBlockSymbolOp>(unit.source);
    if (!source)
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
    default:
      continue;
    }
    auto body = getChildren(source);
    // A procedure owns precisely one source statement. Do not select the first
    // executable descendant of an unknown grouping or wrapper.
    if (body.size() != 1)
      continue;
    auto codeUnit = units.declarations.lookup(source);
    const uint64_t scopeID = codeUnit.getScopeId();
    struct Work {
      Operation *source;
      Owner owner;
      Relation relation;
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
      auto shape = statementShape(work.source, semanticSymbols);
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
      bool scope = shape->kind == Kind::NamedBegin ||
                   shape->kind == Kind::NamedFork ||
                   shape->kind == Kind::ForeachStmt;
      sim::SimStatementDeclOp::create(
          builder, location, id, builder.getI64IntegerAttr(unit.id), scopeID,
          targetKind,
          work.owner.category == sim::VPIStatementSourceKind::Statement
              ? builder.getI64IntegerAttr(work.owner.id)
              : IntegerAttr{},
          shape->name, scope ? builder.getUnitAttr() : UnitAttr{}, UnitAttr{});
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
      sim::SimVPIStatementRelationDeclOp::create(
          builder, location, work.owner.category, work.owner.id, sourceKind,
          selector, ordinal, modes, id);
      sim::set<sim::metadata::VpiStatementId>(work.source,
                                              builder.getI64IntegerAttr(id));
      if (work.owner.category == sim::VPIStatementSourceKind::CodeUnit)
        sim::SimVPIThreadOriginBindingOp::create(builder, location, unit.id,
                                                 id);
      Owner childOwner{sim::VPIStatementSourceKind::Statement, id, shape->kind};
      for (const Child &child : llvm::reverse(shape->children))
        worklist.push_back({child.source, childOwner, child.relation});
    }
  }
  return success();
}
} // namespace obelisk::simlowering
