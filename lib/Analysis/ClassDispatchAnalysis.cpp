//===- ClassDispatchAnalysis.cpp - Managed class dispatch ---------------===//

#include "obelisk/Analysis/ClassDispatchAnalysis.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MathExtras.h"

#include <array>
#include <limits>
#include <utility>

using namespace mlir;

namespace obelisk::analysis {
namespace {

// Immutable Patricia maps share inherited tables. A lookup or update follows
// at most 64 branching bits, independent of inheritance depth and table size.
// Only direct method declarations allocate new paths; there is no per-class
// copy of the full inherited vtable or signature inventory.
template <typename Value> class PersistentMap {
  struct Node : llvm::RefCountedBase<Node> {
    uint64_t prefix = 0;
    uint64_t mask = 0;
    llvm::IntrusiveRefCntPtr<const Node> left, right;
    Value value{};
  };
  using Ptr = llvm::IntrusiveRefCntPtr<const Node>;
  Ptr root;

  explicit PersistentMap(Ptr root) : root(std::move(root)) {}

  static Ptr leaf(uint64_t key, Value value) {
    auto node = llvm::makeIntrusiveRefCnt<Node>();
    node->prefix = key;
    node->value = std::move(value);
    return node;
  }

  static Ptr branch(uint64_t prefix, uint64_t mask, Ptr left, Ptr right) {
    if (!left)
      return right;
    if (!right)
      return left;
    auto node = llvm::makeIntrusiveRefCnt<Node>();
    node->prefix = prefix;
    node->mask = mask;
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
  }

  static uint64_t above(uint64_t key, uint64_t mask) {
    return key & ~(mask | (mask - 1));
  }

  static bool matches(uint64_t key, const Node &node) {
    return above(key, node.mask) == node.prefix;
  }

  static Ptr link(uint64_t key, Ptr value, Ptr previous) {
    uint64_t mask = uint64_t{1}
                    << (63 - llvm::countl_zero(key ^ previous->prefix));
    uint64_t prefix = above(key, mask);
    return key & mask ? branch(prefix, mask, previous, value)
                      : branch(prefix, mask, value, previous);
  }

  static Ptr update(const Ptr &node, uint64_t key, Value value) {
    if (!node)
      return value ? leaf(key, std::move(value)) : Ptr{};
    if (!node->mask) {
      if (node->prefix != key)
        return value ? link(key, leaf(key, std::move(value)), node) : node;
      if (node->value == value)
        return node;
      return value ? leaf(key, std::move(value)) : Ptr{};
    }
    if (!matches(key, *node))
      return value ? link(key, leaf(key, std::move(value)), node) : node;
    if (!(key & node->mask)) {
      Ptr left = update(node->left, key, std::move(value));
      return left == node->left
                 ? node
                 : branch(node->prefix, node->mask, left, node->right);
    }
    Ptr right = update(node->right, key, std::move(value));
    return right == node->right
               ? node
               : branch(node->prefix, node->mask, node->left, right);
  }

public:
  PersistentMap() = default;
  explicit operator bool() const { return bool(root); }
  bool operator==(const PersistentMap &other) const {
    return root == other.root;
  }

  PersistentMap set(uint64_t key, Value value) const {
    return PersistentMap(update(root, key, std::move(value)));
  }

  const Value *find(uint64_t key) const {
    const Node *node = root.get();
    while (node && node->mask) {
      if (!matches(key, *node))
        return nullptr;
      node = (key & node->mask ? node->right : node->left).get();
    }
    return node && node->prefix == key ? &node->value : nullptr;
  }

  Value lookup(uint64_t key) const {
    const Value *value = find(key);
    return value ? *value : Value{};
  }

  Value first() const {
    const Node *node = root.get();
    while (node && node->mask)
      node = node->left.get();
    return node ? node->value : Value{};
  }

  template <typename Visit>
  void visitNodes(llvm::DenseSet<const void *> &visited, Visit visit) const {
    SmallVector<const Node *, 16> pending;
    if (root)
      pending.push_back(root.get());
    while (!pending.empty()) {
      const Node *node = pending.pop_back_val();
      if (!visited.insert(node).second)
        continue;
      if (!node->mask)
        visit(node->value);
      else {
        pending.push_back(node->left.get());
        pending.push_back(node->right.get());
      }
    }
  }
};

void orderClasses(llvm::SmallVectorImpl<sim::SimClassDeclOp> &classes) {
  if (classes.size() < 2)
    return;
  SmallVector<sim::SimClassDeclOp> temporary(classes.size());
  for (unsigned shift = 0; shift != 64; shift += 8) {
    std::array<size_t, 256> offsets{};
    for (auto declaration : classes)
      ++offsets[(declaration.getId() >> shift) & 255];
    size_t begin = 0;
    for (size_t &offset : offsets) {
      size_t count = offset;
      offset = begin;
      begin += count;
    }
    for (auto declaration : classes)
      temporary[offsets[(declaration.getId() >> shift) & 255]++] = declaration;
    classes.swap(temporary);
  }
}

} // namespace

struct ClassDispatchAnalysis::Index {
  using Methods = PersistentMap<sim::SimClassMethodDeclOp>;
  struct Class {
    size_t begin = 0, end = 0;
    bool indexed = false;
    Methods methods;
    PersistentMap<Methods> signatures;
  };
  SmallVector<Class> classes;
  SmallVector<SmallVector<size_t, 2>> parents, descendants;

  explicit Index(size_t count)
      : classes(count), parents(count), descendants(count) {}
};

ClassDispatchAnalysis::ClassDispatchAnalysis(sim::SimDesignOp design) {
  design.walk(
      [&](sim::SimClassDeclOp declaration) { classes.push_back(declaration); });
  orderClasses(classes);
  for (auto [ordinal, declaration] : llvm::enumerate(classes))
    classIndices.try_emplace(declaration.getSymName(), ordinal);

  auto inventory = std::make_shared<Index>(classes.size());
  constexpr size_t noBase = std::numeric_limits<size_t>::max();
  SmallVector<size_t> bases(classes.size(), noBase);
  SmallVector<SmallVector<size_t, 2>> children(classes.size());
  SmallVector<SmallVector<sim::SimClassMethodDeclOp, 2>> methods(
      classes.size());
  auto inherit = [&](size_t child, StringRef name, bool base) {
    auto found = classIndices.find(name);
    if (found == classIndices.end()) {
      if (base)
        bases[child] = classes.size();
      return;
    }
    size_t parent = found->second;
    inventory->parents[child].push_back(parent);
    inventory->descendants[parent].push_back(child);
    if (base) {
      bases[child] = parent;
      children[parent].push_back(child);
    }
  };
  for (auto [ordinal, declaration] : llvm::enumerate(classes)) {
    if (auto base = declaration.getBase())
      inherit(ordinal, *base, true);
    if (ArrayAttr interfaces = declaration.getInterfacesAttr())
      for (Attribute attribute : interfaces)
        if (auto reference = dyn_cast<FlatSymbolRefAttr>(attribute))
          inherit(ordinal, reference.getValue(), false);
  }
  design.walk([&](sim::SimClassMethodDeclOp method) {
    if (auto found = classIndices.find(method.getOwner());
        found != classIndices.end())
      methods[found->second].push_back(method);
  });

  // Single-inheritance subtrees give ordinary instanceof queries constant
  // time. Interface edges remain sparse: a diamond is visited once per query,
  // rather than being expanded into a transitive-closure matrix.
  struct Frame {
    size_t ordinal;
    size_t child = 0;
  };
  size_t preorder = 0;
  for (size_t root = 0; root != classes.size(); ++root) {
    if (bases[root] != noBase)
      continue;
    SmallVector<Frame, 32> pending{{root}};
    while (!pending.empty()) {
      Frame &frame = pending.back();
      auto &entry = inventory->classes[frame.ordinal];
      if (!entry.indexed) {
        entry.indexed = true;
        entry.begin = preorder++;
        if (bases[frame.ordinal] != noBase) {
          const auto &base = inventory->classes[bases[frame.ordinal]];
          entry.methods = base.methods;
          entry.signatures = base.signatures;
        }
        for (auto method : methods[frame.ordinal]) {
          auto slot = method.getSlot();
          if (!slot)
            continue;
          auto previous = entry.methods.lookup(*slot);
          if (previous && previous.getSignatureIdAttr() &&
              (!method.getSignatureIdAttr() ||
               method.getSignatureId() != previous.getSignatureId())) {
            uint64_t signature = *previous.getSignatureId();
            auto selected = entry.signatures.lookup(signature).set(*slot, {});
            entry.signatures = entry.signatures.set(signature, selected);
          }
          entry.methods = entry.methods.set(*slot, method);
          if (auto signature = method.getSignatureId()) {
            auto selected =
                entry.signatures.lookup(*signature).set(*slot, method);
            entry.signatures = entry.signatures.set(*signature, selected);
          }
        }
      }
      if (frame.child != children[frame.ordinal].size()) {
        size_t child = children[frame.ordinal][frame.child++];
        if (!inventory->classes[child].indexed)
          pending.push_back({child});
        continue;
      }
      entry.end = preorder;
      pending.pop_back();
    }
  }
  // Cyclic or missing-base IR is rejected by design verification. Nodes in
  // such components remain unindexed for clients that disable verification.
  index = std::move(inventory);
}

sim::SimClassDeclOp ClassDispatchAnalysis::lookup(StringRef name) const {
  auto found = classIndices.find(name);
  return found == classIndices.end() ? sim::SimClassDeclOp{}
                                     : classes[found->second];
}

sim::SimClassDeclOp
ClassDispatchAnalysis::lookup(sim::ClassHandleType type) const {
  return lookup(type.getClassName().getRootReference());
}

bool ClassDispatchAnalysis::isInstanceOf(sim::SimClassDeclOp dynamicClass,
                                         sim::SimClassDeclOp target) const {
  if (!dynamicClass || !target)
    return false;
  auto dynamic = classIndices.find(dynamicClass.getSymName());
  auto targetIndex = classIndices.find(target.getSymName());
  if (dynamic == classIndices.end() || targetIndex == classIndices.end())
    return false;
  auto targetDeclaration = classes[targetIndex->second];
  if (!targetDeclaration.getIsInterface()) {
    const auto &source = index->classes[dynamic->second];
    const auto &base = index->classes[targetIndex->second];
    return source.indexed && base.indexed && base.begin <= source.begin &&
           source.begin < base.end;
  }
  SmallVector<size_t, 16> pending{dynamic->second};
  llvm::DenseSet<size_t> visited;
  while (!pending.empty()) {
    size_t current = pending.pop_back_val();
    if (current == targetIndex->second)
      return true;
    if (visited.insert(current).second)
      llvm::append_range(pending, index->parents[current]);
  }
  return false;
}

sim::SimClassMethodDeclOp
ClassDispatchAnalysis::resolve(sim::SimClassDeclOp dynamicClass, uint64_t slot,
                               uint64_t signatureId) const {
  if (!dynamicClass)
    return {};
  auto dynamic = classIndices.find(dynamicClass.getSymName());
  if (dynamic == classIndices.end())
    return {};
  const auto &entry = index->classes[dynamic->second];
  if (slot != getInterfaceDispatchSlot())
    return entry.methods.lookup(slot);
  // Borrow the immutable sub-table so concurrent readers do not alter its
  // intrusive reference count merely to resolve an interface signature.
  const auto *signature = entry.signatures.find(signatureId);
  return signature ? signature->first() : sim::SimClassMethodDeclOp{};
}

SmallVector<sim::SimClassDeclOp>
ClassDispatchAnalysis::compatibleConcreteClasses(
    sim::SimClassDeclOp staticClass) const {
  SmallVector<sim::SimClassDeclOp> result;
  if (!staticClass)
    return result;
  auto found = classIndices.find(staticClass.getSymName());
  if (found == classIndices.end())
    return result;
  SmallVector<size_t, 16> pending{found->second};
  llvm::DenseSet<size_t> visited;
  while (!pending.empty()) {
    size_t current = pending.pop_back_val();
    if (!visited.insert(current).second)
      continue;
    auto declaration = classes[current];
    if (!declaration.getIsAbstract() && !declaration.getIsInterface())
      result.push_back(declaration);
    llvm::append_range(pending, index->descendants[current]);
  }
  orderClasses(result);
  return result;
}

SmallVector<sim::SimClassMethodDeclOp>
ClassDispatchAnalysis::compatibleImplementations(
    sim::SimClassDeclOp staticClass, uint64_t slot, uint64_t signatureId,
    bool isTask) const {
  SmallVector<sim::SimClassMethodDeclOp> result;
  llvm::StringSet<> seen;
  for (sim::SimClassDeclOp candidate : compatibleConcreteClasses(staticClass)) {
    sim::SimClassMethodDeclOp method = resolve(candidate, slot, signatureId);
    if (!method || method.getIsPure() || method.getIsTask() != isTask ||
        !method.getImplementationAttr() || !method.getSignatureIdAttr() ||
        method.getSignatureId() != signatureId ||
        !seen.insert(*method.getImplementation()).second)
      continue;
    result.push_back(method);
  }
  return result;
}

size_t ClassDispatchAnalysis::getNumMethodTableNodes() const {
  llvm::DenseSet<const void *> visited;
  auto method = [](sim::SimClassMethodDeclOp) {};
  for (const auto &entry : index->classes) {
    entry.methods.visitNodes(visited, method);
    entry.signatures.visitNodes(visited, [&](const Index::Methods &methods) {
      methods.visitNodes(visited, method);
    });
  }
  return visited.size();
}

} // namespace obelisk::analysis
