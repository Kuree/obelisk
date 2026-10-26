//===- ImplicitLoopScope.h - Exact source loop scope wrappers -----*- C++
//-*-===//
#ifndef OBELISK_DIALECT_IMPLICITLOOPSCOPE_H
#define OBELISK_DIALECT_IMPLICITLOOPSCOPE_H

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/SmallVector.h"

namespace obelisk::loop_scope {
// The frontend distinguishes compiler-created declaration scopes from actual
// source begin/end blocks. Only the exact marked wrapper may share a loop's
// public scope identity and activation lifetime.
template <typename ForOp, typename ForeachOp, typename ListOp,
          typename DeclarationOp, typename BlockOp>
mlir::LogicalResult verify(BlockOp block) {
  if (!block.getIsImplicitLoopScope())
    return mlir::success();
  if (!block.getBlockSymbolAttr() ||
      static_cast<unsigned>(block.getBlockKind()) != 0)
    return block.emitOpError("implicit loop scope requires a sequential block "
                             "and exact block symbol");
  auto children = [](mlir::Operation *op) {
    llvm::SmallVector<mlir::Operation *> result;
    if (op->getNumRegions() != 1 || op->getRegion(0).empty())
      return result;
    for (auto &child : op->getRegion(0).front())
      result.push_back(&child);
    return result;
  };
  auto body = children(block);
  if (body.size() == 1 && mlir::isa<ListOp>(body.front()))
    body = children(body.front());
  if (body.empty())
    return block.emitOpError(
        "implicit loop scope requires one loop after its declarations");
  auto *loop = body.pop_back_val();
  for (auto *declaration : body)
    if (!mlir::isa<DeclarationOp>(declaration))
      return block.emitOpError(
          "implicit loop scope prefix must contain only variable declarations");
  if (auto forLoop = mlir::dyn_cast<ForOp>(loop)) {
    if (!forLoop.getHasLocalVariables() || body.empty())
      return block.emitOpError(
          "implicit for scope requires local loop declarations");
  } else if (!mlir::isa<ForeachOp>(loop) || !body.empty()) {
    return block.emitOpError(
        "implicit loop scope requires a declaration-form for or foreach loop");
  }
  return mlir::success();
}
} // namespace obelisk::loop_scope
#endif
