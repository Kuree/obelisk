//===- DescriptorMap.h - Shared descriptor indexes --------------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_DESCRIPTORMAP_H
#define OBELISK_RUNTIME_DESCRIPTORMAP_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obelisk::runtime {

// Memoize immutable subtree intersections. Registration rolls back entries
// involving unpublished nodes before releasing their descriptor indexes.
// Participating maps and their values must remain immutable and alive until
// the corresponding cache entries are rolled back or the cache is destroyed.
class DescriptorMatchCache {
public:
  struct Key {
    const void *left = nullptr, *right = nullptr;
    bool operator==(const Key &other) const {
      return left == other.left && right == other.right;
    }
  };
  std::optional<size_t> find(Key key) const {
    auto found = counts.find(key);
    return found == counts.end() ? std::nullopt
                                 : std::optional<size_t>(found->second);
  }
  void insert(Key key, size_t count) {
    additions.push_back(key);
    counts.emplace(key, count);
  }
  void commit() { additions.clear(); }
  void rollback() {
    for (Key key : additions)
      counts.erase(key);
    additions.clear();
  }

  struct Hash {
    size_t operator()(Key key) const {
      size_t left = std::hash<const void *>{}(key.left);
      size_t right = std::hash<const void *>{}(key.right);
      return left ^ (right + size_t(UINT64_C(0x9e3779b97f4a7c15)) +
                     (left << 6) + (left >> 2));
    }
  };

private:
  std::unordered_map<Key, size_t, Hash> counts;
  std::vector<Key> additions;
};

// Patricia paths have at most 64 branching bits. Updating a direct declaration
// shares every unchanged subtree, including tables inherited by other classes.
template <typename Value> class DescriptorMap {
  template <typename> friend class DescriptorMap;
  struct Node;
  using Ptr = std::shared_ptr<const Node>;
  struct Node {
    uint64_t prefix = 0, mask = 0;
    Ptr left, right;
    Value value{};
    size_t count = 1;
  };
  Ptr root;

public:
  // Cache immutable unions across inheritance edges. Retain both inputs so
  // pointer keys cannot be reused after an unpublished inventory is released.
  // Registration rolls back new entries before discarding failed metadata.
  class MergeCache {
    friend class DescriptorMap;
    struct Entry {
      Ptr left, right, result;
    };
    using Key = DescriptorMatchCache::Key;
    std::unordered_map<Key, Entry, DescriptorMatchCache::Hash> entries;
    std::vector<Key> additions;

  public:
    size_t size() const { return entries.size(); }
    void commit() { additions.clear(); }
    void rollback() {
      for (Key key : additions)
        entries.erase(key);
      additions.clear();
    }
  };

private:
  explicit DescriptorMap(Ptr root) : root(std::move(root)) {}

  static uint64_t above(uint64_t key, uint64_t mask) {
    return key & ~(mask | (mask - 1));
  }
  static bool matches(uint64_t key, const Node &node) {
    return above(key, node.mask) == node.prefix;
  }
  static Ptr leaf(uint64_t key, Value value) {
    auto node = std::make_shared<Node>();
    node->prefix = key;
    node->value = std::move(value);
    return node;
  }
  static Ptr branch(uint64_t prefix, uint64_t mask, Ptr left, Ptr right) {
    auto node = std::make_shared<Node>();
    node->prefix = prefix;
    node->mask = mask;
    node->count = left->count + right->count;
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
  }
  static Ptr link(uint64_t key, Ptr value, Ptr previous) {
    uint64_t mask = UINT64_C(1)
                    << (63 - __builtin_clzll(key ^ previous->prefix));
    uint64_t prefix = above(key, mask);
    return key & mask ? branch(prefix, mask, previous, value)
                      : branch(prefix, mask, value, previous);
  }
  static Ptr update(const Ptr &node, uint64_t key, Value value) {
    if (!node)
      return leaf(key, std::move(value));
    if (!node->mask) {
      if (node->prefix != key)
        return link(key, leaf(key, std::move(value)), node);
      return node->value == value ? node : leaf(key, std::move(value));
    }
    if (!matches(key, *node))
      return link(key, leaf(key, std::move(value)), node);
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
  static const Node *lowerBound(const Node *node, uint64_t key) {
    if (!node)
      return nullptr;
    if (!node->mask)
      return node->prefix >= key ? node : nullptr;
    if (key < node->prefix || matches(key, *node)) {
      if (key < node->prefix || !(key & node->mask))
        if (const Node *found = lowerBound(node->left.get(), key))
          return found;
      return lowerBound(node->right.get(), key);
    }
    return nullptr;
  }

  template <typename Other, typename Match>
  static size_t countMatches(const Ptr &left,
                             const typename DescriptorMap<Other>::Ptr &right,
                             DescriptorMatchCache &cache, Match &match) {
    if (!left || !right)
      return 0;
    DescriptorMatchCache::Key key{left.get(), right.get()};
    if (auto known = cache.find(key))
      return *known;
    size_t count = 0;
    if (!left->mask && !right->mask) {
      count = left->prefix == right->prefix && match(left->value, right->value);
    } else if (left->mask > right->mask) {
      if (matches(right->prefix, *left))
        count = countMatches<Other>(right->prefix & left->mask ? left->right
                                                               : left->left,
                                    right, cache, match);
    } else if (right->mask > left->mask) {
      if (DescriptorMap<Other>::matches(left->prefix, *right))
        count = countMatches<Other>(
            left, left->prefix & right->mask ? right->right : right->left,
            cache, match);
    } else if (left->prefix == right->prefix) {
      count = countMatches<Other>(left->left, right->left, cache, match) +
              countMatches<Other>(left->right, right->right, cache, match);
    }
    cache.insert(key, count);
    return count;
  }
  static Ptr merge(const Ptr &left, const Ptr &right, MergeCache &cache) {
    if (!right || left == right)
      return left;
    if (!left)
      return right;
    DescriptorMatchCache::Key key{left.get(), right.get()};
    auto found = cache.entries.find(key);
    if (found != cache.entries.end())
      return found->second.result;
    Ptr result = mergeUncached(left, right, cache);
    cache.additions.push_back(key);
    cache.entries.emplace(key, typename MergeCache::Entry{left, right, result});
    return result;
  }
  static Ptr mergeUncached(const Ptr &left, const Ptr &right,
                           MergeCache &cache) {
    if (!right->mask) {
      const Node *found = left.get();
      while (found && found->mask) {
        if (!matches(right->prefix, *found)) {
          found = nullptr;
          break;
        }
        found =
            (right->prefix & found->mask ? found->right : found->left).get();
      }
      if (found && found->prefix == right->prefix)
        return left;
      return update(left, right->prefix, right->value);
    }
    if (left->mask > right->mask && matches(right->prefix, *left)) {
      if (right->prefix & left->mask) {
        Ptr next = merge(left->right, right, cache);
        return next == left->right
                   ? left
                   : branch(left->prefix, left->mask, left->left, next);
      }
      Ptr next = merge(left->left, right, cache);
      return next == left->left
                 ? left
                 : branch(left->prefix, left->mask, next, left->right);
    }
    if (left->mask == right->mask && left->prefix == right->prefix) {
      Ptr first = merge(left->left, right->left, cache);
      Ptr second = merge(left->right, right->right, cache);
      return first == left->left && second == left->right
                 ? left
                 : branch(left->prefix, left->mask, first, second);
    }
    if (left->mask < right->mask && matches(left->prefix, *right)) {
      if (left->prefix & right->mask) {
        return branch(right->prefix, right->mask, right->left,
                      merge(left, right->right, cache));
      }
      return branch(right->prefix, right->mask, merge(left, right->left, cache),
                    right->right);
    }
    return link(right->prefix, right, left);
  }

public:
  DescriptorMap() = default;
  size_t size() const { return root ? root->count : 0; }
  bool empty() const { return !root; }
  DescriptorMap set(uint64_t key, Value value) const {
    return DescriptorMap(update(root, key, std::move(value)));
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
  const Value *lowerBound(uint64_t key) const {
    const Node *node = lowerBound(root.get(), key);
    return node ? &node->value : nullptr;
  }
  const Value *first() const { return lowerBound(0); }
  // Union keeps existing values and shares unchanged subtrees. Reuse the
  // inventory's cache so growing multiple-parent chains do not repeatedly
  // merge every inherited entry.
  DescriptorMap merge(const DescriptorMap &other, MergeCache &cache) const {
    return DescriptorMap(merge(root, other.root, cache));
  }
  template <typename Other, typename Match>
  size_t countMatches(const DescriptorMap<Other> &other,
                      DescriptorMatchCache &cache, Match match) const {
    return countMatches<Other>(root, other.root, cache, match);
  }
  template <typename Visit> void forEach(Visit visit) const {
    std::vector<const Node *> pending;
    if (root)
      pending.push_back(root.get());
    while (!pending.empty()) {
      const Node *node = pending.back();
      pending.pop_back();
      if (!node->mask)
        visit(node->value);
      else {
        pending.push_back(node->right.get());
        pending.push_back(node->left.get());
      }
    }
  }
};

} // namespace obelisk::runtime

#endif // OBELISK_RUNTIME_DESCRIPTORMAP_H
