//===- GraphAlgorithms.h - Shared graph algorithms -------------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_GRAPHALGORITHMS_H
#define OBELISK_ANALYSIS_GRAPHALGORITHMS_H
#include "mlir/Support/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <functional>
#include <optional>
namespace obelisk::analysis {
/// Iterative Tarjan strongly connected components.
///
/// Recursion is deliberately avoided: generated process CFGs reach tens of
/// thousands of blocks, which overflows the stack long before it exhausts
/// memory. Components come back in reverse topological order with sorted
/// members, so every caller gets a deterministic result.
template <typename Node, typename Compare = std::less<Node>>
::mlir::SmallVector<::mlir::SmallVector<Node>>
computeStronglyConnectedComponents(
    ::mlir::ArrayRef<Node> nodes,
    const ::llvm::DenseMap<Node, ::mlir::SmallVector<Node>> &adjacency,
    Compare compare = {}) {
  ::llvm::DenseMap<Node, unsigned> index, lowlink;
  ::llvm::DenseSet<Node> onStack;
  ::mlir::SmallVector<Node> tarjanStack;
  unsigned nextIndex = 0;
  ::mlir::SmallVector<::mlir::SmallVector<Node>> components;

  struct Frame {
    Node node;
    size_t nextSuccessor = 0;
    std::optional<Node> parent;
  };
  for (Node root : nodes) {
    if (index.count(root))
      continue;
    index[root] = lowlink[root] = nextIndex++;
    tarjanStack.push_back(root);
    onStack.insert(root);
    ::mlir::SmallVector<Frame> dfs{{root, 0, std::nullopt}};
    while (!dfs.empty()) {
      Frame &frame = dfs.back();
      auto found = adjacency.find(frame.node);
      ::mlir::ArrayRef<Node> successors =
          found == adjacency.end() ? ::mlir::ArrayRef<Node>()
                                   : ::mlir::ArrayRef<Node>(found->second);
      if (frame.nextSuccessor < successors.size()) {
        Node successor = successors[frame.nextSuccessor++];
        if (!index.count(successor)) {
          index[successor] = lowlink[successor] = nextIndex++;
          tarjanStack.push_back(successor);
          onStack.insert(successor);
          dfs.push_back({successor, 0, frame.node});
        } else if (onStack.contains(successor)) {
          lowlink[frame.node] = std::min(lowlink[frame.node], index[successor]);
        }
        continue;
      }

      Node node = frame.node;
      std::optional<Node> parent = frame.parent;
      dfs.pop_back();
      if (parent)
        lowlink[*parent] = std::min(lowlink[*parent], lowlink[node]);
      if (lowlink[node] != index[node])
        continue;
      ::mlir::SmallVector<Node> component;
      while (true) {
        Node member = tarjanStack.pop_back_val();
        onStack.erase(member);
        component.push_back(member);
        if (member == node)
          break;
      }
      ::llvm::sort(component, compare);
      components.push_back(std::move(component));
    }
  }
  return components;
}

} // namespace obelisk::analysis
#endif
