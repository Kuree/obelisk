//===- ParallelWalk.h - Concurrent IR inventories in walk order -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_PARALLELWALK_H
#define OBELISK_ANALYSIS_PARALLELWALK_H

#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace obelisk::analysis {

/// Runs `func(index)` for each index in [0, count), handing workers
/// contiguous chunks. mlir::parallelFor orders diagnostics per element under a
/// lock, which dominates when there are many small elements.
template <typename Func>
void parallelForChunked(mlir::MLIRContext *context, size_t count,
                        Func &&func) {
  constexpr size_t maxChunks = 256;
  size_t chunks = std::min(count, maxChunks);
  mlir::parallelFor(context, 0, chunks, [&](size_t chunk) {
    for (size_t index = count * chunk / chunks,
                end = count * (chunk + 1) / chunks;
         index != end; ++index)
      func(index);
  });
}

/// Below this many children, a serial walk is cheaper than scheduling.
inline constexpr size_t kParallelWalkMinChildren = 128;

/// The thread that ran static initialization: the driver's main thread.
inline const std::thread::id kParallelWalkMainThread =
    std::this_thread::get_id();

/// Only whole-module and whole-design walks on the main thread fan out.
/// Analyses also run inside per-function workers, where a nested task group
/// would block the worker; there the walk stays serial.
inline bool walksConcurrently(mlir::Operation *root, size_t children) {
  return children >= kParallelWalkMinChildren &&
         std::this_thread::get_id() == kParallelWalkMainThread &&
         root->hasTrait<mlir::OpTrait::SymbolTable>();
}

/// Returns the `OpT` operations under `root`, `root` included, for which
/// `select` holds, in the order `root->walk<Order>` visits them. The subtrees
/// of a module's or design's children are walked concurrently, so `select`
/// may only read IR. Gathering first and then rewriting serially in this order gives the
/// same result as rewriting during a single walk.
template <typename OpT, mlir::WalkOrder Order = mlir::WalkOrder::PostOrder,
          typename Select>
llvm::SmallVector<OpT> collectInWalkOrder(mlir::Operation *root,
                                          Select &&select) {
  llvm::SmallVector<OpT> result;
  llvm::SmallVector<mlir::Operation *> children;
  for (mlir::Region &region : root->getRegions())
    for (mlir::Block &block : region)
      for (mlir::Operation &child : block)
        children.push_back(&child);
  if (!walksConcurrently(root, children.size())) {
    root->walk<Order>([&](OpT op) {
      if (select(op))
        result.push_back(op);
    });
    return result;
  }
  auto self = llvm::dyn_cast<OpT>(root);
  if (Order == mlir::WalkOrder::PreOrder && self && select(self))
    result.push_back(self);
  llvm::SmallVector<llvm::SmallVector<OpT, 0>> found(children.size());
  parallelForChunked(root->getContext(), children.size(), [&](size_t index) {
    children[index]->walk<Order>([&](OpT op) {
      if (select(op))
        found[index].push_back(op);
    });
  });
  for (auto &ops : found)
    llvm::append_range(result, ops);
  if (Order == mlir::WalkOrder::PostOrder && self && select(self))
    result.push_back(self);
  return result;
}

template <typename OpT, mlir::WalkOrder Order = mlir::WalkOrder::PostOrder>
llvm::SmallVector<OpT> collectInWalkOrder(mlir::Operation *root) {
  return collectInWalkOrder<OpT, Order>(root, [](OpT) { return true; });
}

/// Whether any `OpT` under `root`, `root` included, satisfies `select`.
/// Children's subtrees are searched concurrently; `select` may only read IR.
template <typename OpT, typename Select>
bool anyNested(mlir::Operation *root, Select &&select) {
  if (auto self = llvm::dyn_cast<OpT>(root); self && select(self))
    return true;
  llvm::SmallVector<mlir::Operation *> children;
  for (mlir::Region &region : root->getRegions())
    for (mlir::Block &block : region)
      for (mlir::Operation &child : block)
        children.push_back(&child);
  if (!walksConcurrently(root, children.size()))
    return root
        ->walk([&](OpT op) {
          return select(op) ? mlir::WalkResult::interrupt()
                            : mlir::WalkResult::advance();
        })
        .wasInterrupted();
  std::atomic<bool> found = false;
  parallelForChunked(root->getContext(), children.size(), [&](size_t index) {
    if (found)
      return;
    if (children[index]
            ->walk([&](OpT op) {
              return select(op) ? mlir::WalkResult::interrupt()
                                : mlir::WalkResult::advance();
            })
            .wasInterrupted())
      found = true;
  });
  return found;
}

/// The children of `root`, in order, with an operation in their subtree,
/// themselves included, that satisfies `select`. Subtrees are searched
/// concurrently; `select` may only read IR. A partial dialect conversion of
/// just these children legalizes the same operations in the same order as a
/// conversion of `root`, when `select` matches every illegal operation.
template <typename Select>
llvm::SmallVector<mlir::Operation *> childrenContaining(mlir::Operation *root,
                                                        Select &&select) {
  llvm::SmallVector<mlir::Operation *> children;
  for (mlir::Region &region : root->getRegions())
    for (mlir::Block &block : region)
      for (mlir::Operation &child : block)
        children.push_back(&child);
  llvm::SmallVector<char> contains(children.size(), false);
  auto search = [&](size_t index) {
    contains[index] = children[index]
                          ->walk([&](mlir::Operation *operation) {
                            return select(operation)
                                       ? mlir::WalkResult::interrupt()
                                       : mlir::WalkResult::advance();
                          })
                          .wasInterrupted();
  };
  if (walksConcurrently(root, children.size()))
    parallelForChunked(root->getContext(), children.size(), search);
  else
    for (size_t index = 0; index != children.size(); ++index)
      search(index);
  llvm::SmallVector<mlir::Operation *> result;
  for (auto [index, child] : llvm::enumerate(children))
    if (contains[index])
      result.push_back(child);
  return result;
}

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_PARALLELWALK_H
