//===- RangeSet.h - Root-keyed bit ranges and a persistent set --*- C++ -*-===//
//
// Sorted, coalesced bit ranges of storage roots, and a persistent set of them
// for dense dataflow states that carry thousands of roots through every CFG
// edge: copies share the structure, an update copies one path, and a join
// costs only what the two states do not share.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_ANALYSIS_RANGESET_H
#define TALOS_ANALYSIS_RANGESET_H

#include "mlir/Support/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>

namespace talos::analysis {

/// `width` bits of root `root` from bit `low`.
struct Range {
  uint64_t root, low, width;
  bool operator==(const Range &other) const {
    return root == other.root && low == other.low && width == other.width;
  }
};
using Ranges = llvm::SmallVector<Range>;

inline void normalize(Ranges &ranges) {
  llvm::sort(ranges, [](const Range &a, const Range &b) {
    return std::tie(a.root, a.low, a.width) < std::tie(b.root, b.low, b.width);
  });
  Ranges merged;
  for (const Range &r : ranges) {
    if (!r.width)
      continue;
    if (!merged.empty() && merged.back().root == r.root &&
        r.low <= merged.back().low + merged.back().width) {
      auto &last = merged.back();
      last.width = std::max(last.low + last.width, r.low + r.width) - last.low;
    } else
      merged.push_back(r);
  }
  ranges = std::move(merged);
}
// Every range set below is sorted by (root, low), with overlapping and
// adjacent intervals coalesced. Merge the sets linearly: a fused activation
// can have thousands of roots at every CFG join.
inline void appendRange(Ranges &result, Range range) {
  if (!range.width)
    return;
  if (!result.empty() && result.back().root == range.root &&
      range.low <= result.back().low + result.back().width) {
    auto &last = result.back();
    last.width =
        std::max(last.low + last.width, range.low + range.width) - last.low;
  } else
    result.push_back(range);
}
inline void unite(Ranges &target, llvm::ArrayRef<Range> incoming) {
  Ranges result;
  result.reserve(target.size() + incoming.size());
  size_t a = 0, b = 0;
  while (a != target.size() || b != incoming.size()) {
    bool takeA =
        b == incoming.size() ||
        (a != target.size() && std::tie(target[a].root, target[a].low) <
                                   std::tie(incoming[b].root, incoming[b].low));
    appendRange(result, takeA ? target[a++] : incoming[b++]);
  }
  target = std::move(result);
}
inline Ranges intersect(llvm::ArrayRef<Range> a, llvm::ArrayRef<Range> b) {
  Ranges result;
  size_t i = 0, j = 0;
  while (i != a.size() && j != b.size()) {
    const auto &x = a[i];
    const auto &y = b[j];
    if (x.root < y.root) {
      ++i;
      continue;
    }
    if (y.root < x.root) {
      ++j;
      continue;
    }
    uint64_t low = std::max(x.low, y.low);
    uint64_t high = std::min(x.low + x.width, y.low + y.width);
    if (low < high)
      appendRange(result, {x.root, low, high - low});
    if (x.low + x.width <= y.low + y.width)
      ++i;
    else
      ++j;
  }
  return result;
}
inline Ranges subtract(llvm::ArrayRef<Range> a, llvm::ArrayRef<Range> b) {
  Ranges result;
  size_t j = 0;
  for (const Range &x : a) {
    uint64_t cursor = x.low, end = x.low + x.width;
    while (j != b.size() &&
           (b[j].root < x.root ||
            (b[j].root == x.root && b[j].low + b[j].width <= cursor)))
      ++j;
    for (size_t k = j; k != b.size() && b[k].root == x.root && b[k].low < end;
         ++k) {
      const Range &y = b[k];
      if (cursor < y.low)
        result.push_back({x.root, cursor, y.low - cursor});
      cursor = std::min(end, std::max(cursor, y.low + y.width));
      if (cursor == end)
        break;
    }
    if (cursor < end)
      result.push_back({x.root, cursor, end - cursor});
  }
  return result;
}
/// A persistent set of root lanes for the dense activation state. It is a
/// big-endian Patricia trie keyed by root (Okasaki and Gill, "Fast Mergeable
/// Integer Maps"); each leaf holds that root's sorted, coalesced intervals, so
/// in-order traversal yields exactly the normalized Ranges form. A fused
/// activation carries thousands of written roots through every CFG edge.
/// Copies share the trie, an update copies one path, and unions and
/// intersections return shared or unchanged subtrees as they are. A join
/// therefore costs only what the two states do not share.
class RangeSet {
  struct Node : llvm::RefCountedBase<Node> {
    /// Leaf key, or the bits above `mask` for a branch.
    uint64_t prefix = 0;
    /// The branching bit of a branch; zero for a leaf.
    uint64_t mask = 0;
    llvm::IntrusiveRefCntPtr<const Node> left, right;
    Ranges lanes;
    bool isLeaf() const { return mask == 0; }
  };
  using Ptr = llvm::IntrusiveRefCntPtr<const Node>;
  Ptr root;

  explicit RangeSet(Ptr root) : root(std::move(root)) {}
  static Ptr leaf(uint64_t key, Ranges lanes) {
    auto node = llvm::makeIntrusiveRefCnt<Node>();
    node->prefix = key;
    node->lanes = std::move(lanes);
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
  static const Node *find(const Node *node, uint64_t key) {
    while (node && !node->isLeaf()) {
      if (!matches(key, *node))
        return nullptr;
      node = (isLeft(key, node->mask) ? node->left : node->right).get();
    }
    return node && node->prefix == key ? node : nullptr;
  }
  /// Unites `lanes` (normalized, all of root `key`) into the trie.
  static Ptr insert(const Ptr &node, uint64_t key,
                    llvm::ArrayRef<Range> lanes) {
    if (!node)
      return leaf(key, Ranges(lanes));
    if (node->isLeaf()) {
      if (node->prefix != key)
        return link(key, leaf(key, Ranges(lanes)), node->prefix, node);
      Ranges merged = node->lanes;
      ::talos::analysis::unite(merged, lanes);
      return merged == node->lanes ? node : leaf(key, std::move(merged));
    }
    if (!matches(key, *node))
      return link(key, leaf(key, Ranges(lanes)), node->prefix, node);
    if (isLeft(key, node->mask)) {
      Ptr left = insert(node->left, key, lanes);
      return left == node->left
                 ? node
                 : branch(node->prefix, node->mask, left, node->right);
    }
    Ptr right = insert(node->right, key, lanes);
    return right == node->right
               ? node
               : branch(node->prefix, node->mask, node->left, right);
  }
  /// Replaces the lanes of existing leaf `key` with `lanes`, dropping the
  /// leaf when they are empty.
  static Ptr replace(const Ptr &node, uint64_t key, Ranges lanes) {
    if (!node)
      return nullptr;
    if (node->isLeaf())
      return node->prefix != key ? node
             : lanes.empty()     ? nullptr
                                 : leaf(key, std::move(lanes));
    if (!matches(key, *node))
      return node;
    if (isLeft(key, node->mask))
      return branch(node->prefix, node->mask,
                    replace(node->left, key, std::move(lanes)), node->right);
    return branch(node->prefix, node->mask, node->left,
                  replace(node->right, key, std::move(lanes)));
  }
  /// Union that returns `s` itself when `t` adds nothing to it.
  static Ptr uniteNodes(const Ptr &s, const Ptr &t) {
    if (s == t || !t)
      return s;
    if (!s)
      return t;
    if (t->isLeaf())
      return insert(s, t->prefix, t->lanes);
    if (s->isLeaf()) {
      // `t` is a branch, so the union differs from the leaf `s`.
      if (const Node *same = find(t.get(), s->prefix)) {
        Ranges merged = same->lanes;
        ::talos::analysis::unite(merged, s->lanes);
        if (merged == same->lanes)
          return t;
      }
      return insert(t, s->prefix, s->lanes);
    }
    if (s->mask == t->mask && s->prefix == t->prefix) {
      Ptr left = uniteNodes(s->left, t->left),
          right = uniteNodes(s->right, t->right);
      return left == s->left && right == s->right
                 ? s
                 : branch(s->prefix, s->mask, left, right);
    }
    if (s->mask > t->mask && matches(t->prefix, *s)) {
      if (isLeft(t->prefix, s->mask)) {
        Ptr left = uniteNodes(s->left, t);
        return left == s->left ? s : branch(s->prefix, s->mask, left, s->right);
      }
      Ptr right = uniteNodes(s->right, t);
      return right == s->right ? s : branch(s->prefix, s->mask, s->left, right);
    }
    if (t->mask > s->mask && matches(s->prefix, *t))
      return isLeft(s->prefix, t->mask)
                 ? branch(t->prefix, t->mask, uniteNodes(s, t->left), t->right)
                 : branch(t->prefix, t->mask, t->left, uniteNodes(s, t->right));
    return link(s->prefix, s, t->prefix, t);
  }
  /// Intersection that returns `s` itself when it is contained in `t`.
  static Ptr intersectNodes(const Ptr &s, const Ptr &t) {
    if (s == t)
      return s;
    if (!s || !t)
      return nullptr;
    if (s->isLeaf()) {
      const Node *other = find(t.get(), s->prefix);
      if (!other)
        return nullptr;
      Ranges common = intersect(s->lanes, other->lanes);
      if (common == s->lanes)
        return s;
      return common.empty() ? nullptr : leaf(s->prefix, std::move(common));
    }
    if (t->isLeaf()) {
      const Node *other = find(s.get(), t->prefix);
      if (!other)
        return nullptr;
      Ranges common = intersect(other->lanes, t->lanes);
      if (common == other->lanes)
        return Ptr(other);
      return common.empty() ? nullptr : leaf(t->prefix, std::move(common));
    }
    if (s->mask == t->mask && s->prefix == t->prefix) {
      Ptr left = intersectNodes(s->left, t->left);
      Ptr right = intersectNodes(s->right, t->right);
      return left == s->left && right == s->right
                 ? s
                 : branch(s->prefix, s->mask, left, right);
    }
    if (s->mask > t->mask && matches(t->prefix, *s))
      return intersectNodes(isLeft(t->prefix, s->mask) ? s->left : s->right, t);
    if (t->mask > s->mask && matches(s->prefix, *t))
      return intersectNodes(s, isLeft(s->prefix, t->mask) ? t->left : t->right);
    return nullptr;
  }
  static bool equal(const Node *a, const Node *b) {
    if (a == b)
      return true;
    if (!a || !b || a->mask != b->mask || a->prefix != b->prefix)
      return false;
    if (a->isLeaf())
      return a->lanes == b->lanes;
    return equal(a->left.get(), b->left.get()) &&
           equal(a->right.get(), b->right.get());
  }
  static void append(const Node *node, Ranges &result) {
    if (!node)
      return;
    if (node->isLeaf()) {
      llvm::append_range(result, node->lanes);
      return;
    }
    append(node->left.get(), result);
    append(node->right.get(), result);
  }
  /// Calls `function` with each root's normalized slice of sorted `ranges`.
  template <typename Function>
  static void forEachRoot(llvm::ArrayRef<Range> ranges, Function function) {
    while (!ranges.empty()) {
      size_t count = 1;
      while (count != ranges.size() && ranges[count].root == ranges[0].root)
        ++count;
      Ranges slice(ranges.take_front(count));
      normalize(slice);
      function(ranges[0].root, slice);
      ranges = ranges.drop_front(count);
    }
  }

public:
  RangeSet() = default;
  bool operator==(const RangeSet &other) const {
    return equal(root.get(), other.root.get());
  }
  bool operator!=(const RangeSet &other) const { return !(*this == other); }
  /// Adds `ranges`, which are sorted by root as every access set is.
  void unite(llvm::ArrayRef<Range> ranges) {
    forEachRoot(ranges, [&](uint64_t key, llvm::ArrayRef<Range> lanes) {
      if (!lanes.empty())
        root = insert(root, key, lanes);
    });
  }
  /// Returns `ranges \ this`, matching `subtract(ranges, toRanges())`.
  Ranges subtractFrom(llvm::ArrayRef<Range> ranges) const {
    Ranges result;
    for (const Range &range : ranges) {
      const Node *found = find(root.get(), range.root);
      llvm::append_range(result, found ? subtract({range}, found->lanes)
                                       : Ranges{range});
    }
    return result;
  }
  /// Returns `ranges ∩ this`, matching `intersect(ranges, toRanges())`.
  Ranges intersectWith(llvm::ArrayRef<Range> ranges) const {
    Ranges result;
    forEachRoot(ranges, [&](uint64_t key, llvm::ArrayRef<Range> lanes) {
      if (const Node *found = find(root.get(), key))
        llvm::append_range(result, intersect(lanes, found->lanes));
    });
    return result;
  }
  /// Joins `other` into this set. Returns whether this set changed.
  bool uniteWith(const RangeSet &other) {
    Ptr joined = uniteNodes(root, other.root);
    bool changed = !equal(joined.get(), root.get());
    root = std::move(joined);
    return changed;
  }
  bool intersectWith(const RangeSet &other) {
    Ptr joined = intersectNodes(root, other.root);
    bool changed = !equal(joined.get(), root.get());
    root = std::move(joined);
    return changed;
  }
  Ranges toRanges() const {
    Ranges result;
    append(root.get(), result);
    return result;
  }
  /// The sorted, coalesced lanes of root `key`.
  llvm::ArrayRef<Range> lanes(uint64_t key) const {
    const Node *found = find(root.get(), key);
    return found ? llvm::ArrayRef<Range>(found->lanes)
                 : llvm::ArrayRef<Range>();
  }
  /// Removes `ranges`, sorted by root, from the set.
  void erase(llvm::ArrayRef<Range> ranges) {
    forEachRoot(ranges, [&](uint64_t key, llvm::ArrayRef<Range> lanes) {
      const Node *found = find(root.get(), key);
      if (!found || lanes.empty())
        return;
      Ranges remaining = subtract(found->lanes, lanes);
      if (remaining != found->lanes)
        root = replace(root, key, std::move(remaining));
    });
  }
  bool empty() const { return !root; }
  void clear() { root = nullptr; }
  /// Whether both sets share the same tree. Equal sets built separately may
  /// differ in identity.
  bool sameAs(const RangeSet &other) const { return root == other.root; }
};

} // namespace talos::analysis

#endif // TALOS_ANALYSIS_RANGESET_H
