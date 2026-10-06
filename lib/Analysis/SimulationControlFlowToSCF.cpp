//===- SimulationControlFlowToSCF.cpp - Fixed-width CFG structuring
//--------===//
#include "obelisk/Analysis/SimulationControlFlowToSCF.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
using namespace mlir;
namespace obelisk::analysis {
FailureOr<Operation *>
SimulationControlFlowToSCF::createStructuredBranchRegionOp(
    OpBuilder &builder, Operation *op, TypeRange types,
    MutableArrayRef<Region> regions) {
  auto dispatch = dyn_cast<cf::SwitchOp>(op);
  if (!dispatch)
    return ControlFlowToSCFTransformation::createStructuredBranchRegionOp(
        builder, op, types, regions);
  if (!dispatch.getCaseValues() || dispatch.getCaseValues()->empty()) {
    auto execute = scf::ExecuteRegionOp::create(builder, op->getLoc(), types);
    execute.getRegion().takeBody(regions[0]);
    return execute.getOperation();
  }
  OpBuilder::InsertionGuard guard(builder);
  SmallVector<scf::IfOp> branches;
  for (auto [i, value] :
       llvm::enumerate(dispatch.getCaseValues()->getValues<APInt>())) {
    Value constant = arith::ConstantOp::create(
        builder, op->getLoc(), dispatch.getFlag().getType(),
        builder.getIntegerAttr(dispatch.getFlag().getType(), value));
    Value matches =
        arith::CmpIOp::create(builder, op->getLoc(), arith::CmpIPredicate::eq,
                              dispatch.getFlag(), constant);
    auto branch = scf::IfOp::create(builder, op->getLoc(), types, matches);
    branch.getThenRegion().takeBody(regions[i + 1]);
    branches.push_back(branch);
    builder.createBlock(&branch.getElseRegion());
  }
  if (branches.empty())
    return failure();
  branches.back().getElseRegion().takeBody(regions[0]);
  for (unsigned i = 0; i + 1 < branches.size(); ++i) {
    builder.setInsertionPointToEnd(&branches[i].getElseRegion().front());
    scf::YieldOp::create(builder, op->getLoc(), branches[i + 1].getResults());
  }
  return branches.front().getOperation();
}
Value SimulationControlFlowToSCF::getUndefValue(Location loc,
                                                OpBuilder &builder, Type type) {
  // Region SSA needs a value for slots on paths where the source CFG never
  // defined or used them. A defined representative keeps those dead slots
  // representable in both the native and bytecode backends.
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, loc, type,
                                     builder.getIntegerAttr(type, 0));
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    auto plane = builder.getIntegerType(logic.getWidth());
    return sim::SimLogicConstantOp::create(builder, loc, logic,
                                           builder.getIntegerAttr(plane, 0),
                                           builder.getIntegerAttr(plane, 0));
  }
  if (sim::isAggregateType(type))
    return sim::SimAggregateDefaultOp::create(builder, loc, type);
  return ControlFlowToSCFTransformation::getUndefValue(loc, builder, type);
}

} // namespace obelisk::analysis
