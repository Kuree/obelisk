//===- PersistentIntMap.h - Persistent map on 64-bit keys ---------*- C++
//-*-===//
//
// A persistent map from 64-bit keys to values for dense dataflow states: a
// big-endian Patricia trie (Okasaki and Gill, "Fast Mergeable Integer Maps")
// whose copies share structure, whose update copies one root-to-leaf path,
// and whose structural merge of two maps returns shared or unchanged
// subtrees as they are, so it costs only what the maps do not share. A
// state keyed by SSA values uses their opaque pointers as keys.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_ANALYSIS_PERSISTENTINTMAP_H
#define TALOS_ANALYSIS_PERSISTENTINTMAP_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/bit.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace talos::analysis {

template <typename ValueT> class PersistentIntMap {
  struct Node : llvm::RefCountedBase<Node> {
    /// Leaf key, or the bits above `mask` for a branch.
    uint64_t prefix = 0;
    /// The branching bit of a branch; zero for a leaf.
    uint64_t mask = 0;
    llvm::IntrusiveRefCntPtr<const Node> left, right;
    std::optional<ValueT> value;
    bool isLeaf() const { return mask == 0; }
  };
  using Ptr = llvm::IntrusiveRefCntPtr<const Node>;
  Ptr root;

  static Ptr leaf(uint64_t key, ValueT value) {
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
  static bool isLeft(uint64_t key, uint64_t mask) { return !(key & mask); }
  /// Joins two disjoint subtrees under their highest differing bit.
  static Ptr link(uint64_t p0, Ptr t0, uint64_t p1, Ptr t1) {
    uint64_t mask = uint64_t(1) << (63 - llvm::countl_zero(p0 ^ p1));
    return isLeft(p0, mask) ? branch(above(p0, mask), mask, t0, t1)
                            : branch(above(p0, mask), mask, t1, t0);
  }
  static const Node *findNode(const Node *node, uint64_t key) {
    while (node && !node->isLeaf()) {
      if (!matches(key, *node))
        return nullptr;
      node = (isLeft(key, node->mask) ? node->left : node->right).get();
    }
    return node && node->prefix == key ? node : nullptr;
  }
  /// Inserts or replaces `key`, unchanged when it already holds `value`.
  static Ptr insertNode(const Ptr &node, uint64_t key, ValueT &value) {
    if (!node)
      return leaf(key, std::move(value));
    if (node->isLeaf()) {
      if (node->prefix != key)
        return link(key, leaf(key, std::move(value)), node->prefix, node);
      if (*node->value == value)
        return node;
      return leaf(key, std::move(value));
    }
    if (!matches(key, *node))
      return link(key, leaf(key, std::move(value)), node->prefix, node);
    if (isLeft(key, node->mask)) {
      Ptr left = insertNode(node->left, key, value);
      return left == node->left
                 ? node
                 : branch(node->prefix, node->mask, left, node->right);
    }
    Ptr right = insertNode(node->right, key, value);
    return right == node->right
               ? node
               : branch(node->prefix, node->mask, node->left, right);
  }
  static Ptr eraseNode(const Ptr &node, uint64_t key) {
    if (!node)
      return nullptr;
    if (node->isLeaf())
      return node->prefix == key ? nullptr : node;
    if (!matches(key, *node))
      return node;
    if (isLeft(key, node->mask)) {
      Ptr left = eraseNode(node->left, key);
      return left == node->left
                 ? node
                 : branch(node->prefix, node->mask, left, node->right);
    }
    Ptr right = eraseNode(node->right, key);
    return right == node->right
               ? node
               : branch(node->prefix, node->mask, node->left, right);
  }
  /// Keeps the keys of `s` that `t` holds as well, each with
  /// `combine(mine, theirs)` or dropped when that is empty. Returns `s`
  /// itself when nothing changes.
  template <typename Combine>
  static Ptr intersectNodes(const Ptr &s, const Ptr &t, Combine &combine) {
    if (!s || !t)
      return nullptr;
    if (s->isLeaf()) {
      const Node *other = findNode(t.get(), s->prefix);
      if (!other)
        return nullptr;
      std::optional<ValueT> next = combine(*s->value, *other->value);
      if (!next)
        return nullptr;
      return *next == *s->value ? s : leaf(s->prefix, std::move(*next));
    }
    if (t->isLeaf()) {
      const Node *mine = findNode(s.get(), t->prefix);
      if (!mine)
        return nullptr;
      std::optional<ValueT> next = combine(*mine->value, *t->value);
      if (!next)
        return nullptr;
      return *next == *mine->value ? Ptr(mine)
                                   : leaf(t->prefix, std::move(*next));
    }
    if (s->mask == t->mask && s->prefix == t->prefix) {
      Ptr left = intersectNodes(s->left, t->left, combine);
      Ptr right = intersectNodes(s->right, t->right, combine);
      return left == s->left && right == s->right
                 ? s
                 : branch(s->prefix, s->mask, left, right);
    }
    if (s->mask > t->mask && matches(t->prefix, *s))
      return intersectNodes(isLeft(t->prefix, s->mask) ? s->left : s->right, t,
                            combine);
    if (t->mask > s->mask && matches(s->prefix, *t))
      return intersectNodes(s, isLeft(s->prefix, t->mask) ? t->left : t->right,
                            combine);
    return nullptr;
  }
  static size_t sizeOf(const Node *node) {
    if (!node)
      return 0;
    if (node->isLeaf())
      return 1;
    return sizeOf(node->left.get()) + sizeOf(node->right.get());
  }
  template <typename Fn> static void visit(const Node *node, Fn &fn) {
    if (!node)
      return;
    if (node->isLeaf()) {
      fn(node->prefix, *node->value);
      return;
    }
    visit(node->left.get(), fn);
    visit(node->right.get(), fn);
  }

public:
  PersistentIntMap() = default;
  size_t size() const { return sizeOf(root.get()); }
  bool empty() const { return !root; }
  /// Whether both maps share the same tree; equal maps built separately may
  /// differ in identity.
  bool sameAs(const PersistentIntMap &other) const {
    return root == other.root;
  }
  const ValueT *find(uint64_t key) const {
    const Node *node = findNode(root.get(), key);
    return node ? &*node->value : nullptr;
  }
  bool contains(uint64_t key) const { return findNode(root.get(), key); }
  void insert(uint64_t key, ValueT value) {
    root = insertNode(root, key, value);
  }
  void erase(uint64_t key) { root = eraseNode(root, key); }
  /// Restricts this map to the keys `other` holds too, replacing each value
  /// with `combine(mine, theirs)` or dropping it when that is empty. Returns
  /// whether the map changed.
  template <typename Combine>
  bool intersectWith(const PersistentIntMap &other, Combine combine) {
    Ptr merged = intersectNodes(root, other.root, combine);
    bool changed = merged != root;
    root = std::move(merged);
    return changed;
  }
  /// Calls `fn(key, value)` for every entry in key order.
  template <typename Fn> void forEach(Fn fn) const { visit(root.get(), fn); }
};

} // namespace talos::analysis

#endif // TALOS_ANALYSIS_PERSISTENTINTMAP_H
