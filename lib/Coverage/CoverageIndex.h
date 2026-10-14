//===- CoverageIndex.h - bounded-depth indexes for the portable codec -----===//

#ifndef OBELISK_LIB_COVERAGE_COVERAGEINDEX_H
#define OBELISK_LIB_COVERAGE_COVERAGEINDEX_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace obelisk::coverage::detail {

// Prefix-free, order-preserving keys. Integers are big endian; signed keys
// flip their sign bit. Strings and vectors have explicit element/end markers.
// Fixed-width reference lookup has a fixed depth even for adversarial IDs.
// Variable-width keys cost O(key bytes), including their common prefixes.
template <typename T, typename Enable = void> struct IndexKey;

// Encoded lookup keys up to InlineSize bytes stay on the stack, so finding an
// existing key does not allocate. Longer keys move to the heap once.
class IndexKeyBytes {
  static constexpr size_t InlineSize = 64;
  uint8_t inlineBytes[InlineSize];
  std::vector<uint8_t> heap;
  size_t length = 0;

public:
  void push_back(uint8_t byte) {
    if (length < InlineSize) {
      inlineBytes[length++] = byte;
      return;
    }
    if (heap.empty())
      heap.assign(inlineBytes, inlineBytes + InlineSize);
    heap.push_back(byte);
    ++length;
  }
  const uint8_t *begin() const {
    return length <= InlineSize ? inlineBytes : heap.data();
  }
  const uint8_t *end() const { return begin() + length; }
  size_t size() const { return length; }
  uint8_t operator[](size_t index) const { return begin()[index]; }
};
template <typename T>
struct IndexKey<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
  template <typename Bytes>
  static void append(Bytes &bytes, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    if constexpr (std::is_signed_v<T>)
      bits ^= U{1} << (sizeof(T) * 8 - 1);
    for (size_t index = sizeof(T); index != 0; --index)
      bytes.push_back(static_cast<uint8_t>(bits >> ((index - 1) * 8)));
  }
};
template <> struct IndexKey<bool> {
  template <typename Bytes>
  static void append(Bytes &bytes, bool value) {
    bytes.push_back(value);
  }
};
template <typename T> struct IndexKey<T, std::enable_if_t<std::is_enum_v<T>>> {
  template <typename Bytes>
  static void append(Bytes &bytes, T value) {
    IndexKey<std::underlying_type_t<T>>::append(
        bytes, static_cast<std::underlying_type_t<T>>(value));
  }
};
template <> struct IndexKey<std::string> {
  template <typename Bytes>
  static void append(Bytes &bytes, const std::string &value) {
    for (unsigned char byte : value) {
      bytes.push_back(1);
      bytes.push_back(byte);
    }
    bytes.push_back(0);
  }
};
template <typename T, size_t N> struct IndexKey<std::array<T, N>> {
  template <typename Bytes>
  static void append(Bytes &bytes,
                     const std::array<T, N> &value) {
    for (const auto &element : value)
      IndexKey<T>::append(bytes, element);
  }
};
template <typename T> struct IndexKey<std::vector<T>> {
  template <typename Bytes>
  static void append(Bytes &bytes, const std::vector<T> &value) {
    for (const auto &element : value) {
      bytes.push_back(1);
      IndexKey<T>::append(bytes, element);
    }
    bytes.push_back(0);
  }
};
template <typename... T> struct IndexKey<std::tuple<T...>> {
  template <typename Bytes>
  static void append(Bytes &bytes,
                     const std::tuple<T...> &value) {
    std::apply(
        [&](const auto &...element) {
          (IndexKey<std::decay_t<decltype(element)>>::append(bytes, element),
           ...);
        },
        value);
  }
};
template <typename A, typename B> struct IndexKey<std::pair<A, B>> {
  template <typename Bytes>
  static void append(Bytes &bytes,
                     const std::pair<A, B> &value) {
    IndexKey<std::decay_t<A>>::append(bytes, value.first);
    IndexKey<std::decay_t<B>>::append(bytes, value.second);
  }
};

// A compressed binary radix tree, with linked leaves for linear ordered
// iteration. It stores two nodes per distinct key, without a dense fanout at
// every byte and without hash collisions or comparison-tree logarithms.
template <typename Key, typename Value> class IndexMap {
  static constexpr size_t None = std::numeric_limits<size_t>::max();
  struct Entry {
    std::pair<const Key, Value> value;
    std::vector<uint8_t> bytes;
    Entry *previous = nullptr;
    Entry *next = nullptr;
    template <typename... Args>
    Entry(Key key, std::vector<uint8_t> bytes, Args &&...args)
        : value(std::piecewise_construct, std::forward_as_tuple(std::move(key)),
                std::forward_as_tuple(std::forward<Args>(args)...)),
          bytes(std::move(bytes)) {}
  };
  struct Node {
    size_t bit = None;
    size_t child[2] = {None, None};
    Entry *first = nullptr;
    Entry *last = nullptr;
  };
  struct InsertionUndo {
    size_t root;
    size_t nodeCount;
    size_t entryCount;
    size_t subtree;
    std::vector<size_t> path;
  };
  std::deque<Entry> entries;
  std::vector<Node> nodes;
  size_t root = None;
  Entry *first = nullptr;
  Entry *last = nullptr;

  template <typename Bytes>
  static unsigned branch(const Bytes &key, size_t bit) {
    assert(bit / 8 < key.size());
    return (key[bit / 8] >> (7 - bit % 8)) & 1;
  }
  static bool sameBytes(const std::vector<uint8_t> &stored,
                        const IndexKeyBytes &key) {
    return stored.size() == key.size() &&
           std::equal(key.begin(), key.end(), stored.begin());
  }
  Entry *lookup(const IndexKeyBytes &key) const {
    if (root == None)
      return nullptr;
    size_t node = root;
    while (nodes[node].bit != None) {
      if (nodes[node].bit / 8 >= key.size())
        return nullptr;
      node = nodes[node].child[branch(key, nodes[node].bit)];
    }
    Entry *entry = nodes[node].first;
    return sameBytes(entry->bytes, key) ? entry : nullptr;
  }
  Entry *lookupBound(const IndexKeyBytes &key, bool after) const {
    if (root == None)
      return nullptr;
    size_t position = root, prefix = 0;
    while (true) {
      const Node &node = nodes[position];
      const auto &representative = node.first->bytes;
      size_t end = node.bit == None ? representative.size() * 8 : node.bit;
      // Patricia nodes skip shared prefix bits. Compare each skipped segment
      // once, so a variable-width bound costs O(key bytes), not O(depth*bytes).
      while (prefix < end) {
        size_t byte = prefix / 8;
        if (byte >= key.size())
          return node.first;
        size_t next = std::min(end, (byte + 1) * 8);
        uint8_t mask = static_cast<uint8_t>((0xffu >> (prefix % 8)) &
                                            (0xffu << ((byte + 1) * 8 - next)));
        uint8_t actual = key[byte] & mask;
        uint8_t expected = representative[byte] & mask;
        if (actual != expected)
          return actual < expected ? node.first : node.last->next;
        prefix = next;
      }
      if (node.bit == None)
        return key.size() > representative.size() || after ? node.first->next
                                                           : node.first;
      if (node.bit / 8 >= key.size())
        return node.first;
      position = node.child[branch(key, node.bit)];
      prefix = node.bit + 1;
    }
  }

public:
  template <bool Const> class Iterator {
    using Owner = std::conditional_t<Const, const IndexMap, IndexMap>;
    Owner *owner = nullptr;
    Entry *entry = nullptr;
    friend class IndexMap;
    template <bool> friend class Iterator;
    Iterator(Owner *owner, Entry *entry) : owner(owner), entry(entry) {}

  public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<const Key, Value>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<Const, const value_type *, value_type *>;
    using reference =
        std::conditional_t<Const, const value_type &, value_type &>;
    Iterator() = default;
    template <bool C = Const, std::enable_if_t<C, int> = 0>
    Iterator(const Iterator<false> &other)
        : owner(other.owner), entry(other.entry) {}
    reference operator*() const { return entry->value; }
    pointer operator->() const { return &entry->value; }
    Iterator &operator++() {
      entry = entry->next;
      return *this;
    }
    Iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    Iterator &operator--() {
      entry = entry ? entry->previous : owner->last;
      return *this;
    }
    Iterator operator--(int) {
      auto old = *this;
      --*this;
      return old;
    }
    bool operator==(const Iterator &other) const {
      return entry == other.entry;
    }
    bool operator!=(const Iterator &other) const { return !(*this == other); }
  };
  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;
  IndexMap() = default;
  IndexMap(const IndexMap &other) {
    for (const auto &entry : other)
      emplace(entry.first, entry.second);
  }
  IndexMap(IndexMap &&other) noexcept { swap(other); }
  IndexMap &operator=(IndexMap other) {
    swap(other);
    return *this;
  }
  void swap(IndexMap &other) noexcept {
    entries.swap(other.entries);
    nodes.swap(other.nodes);
    std::swap(root, other.root);
    std::swap(first, other.first);
    std::swap(last, other.last);
  }
  size_t size() const { return entries.size(); }
  bool empty() const { return entries.empty(); }
  bool operator==(const IndexMap &other) const {
    return size() == other.size() && std::equal(begin(), end(), other.begin());
  }
  bool operator!=(const IndexMap &other) const { return !(*this == other); }
  iterator begin() { return {this, first}; }
  iterator end() { return {this, nullptr}; }
  const_iterator begin() const { return {this, first}; }
  const_iterator end() const { return {this, nullptr}; }
  iterator find(const Key &key) {
    IndexKeyBytes bytes;
    IndexKey<Key>::append(bytes, key);
    return {this, lookup(bytes)};
  }
  const_iterator find(const Key &key) const {
    IndexKeyBytes bytes;
    IndexKey<Key>::append(bytes, key);
    return {this, lookup(bytes)};
  }
  iterator lower_bound(const Key &key) {
    IndexKeyBytes bytes;
    IndexKey<Key>::append(bytes, key);
    return {this, lookupBound(bytes, false)};
  }
  const_iterator lower_bound(const Key &key) const {
    IndexKeyBytes bytes;
    IndexKey<Key>::append(bytes, key);
    return {this, lookupBound(bytes, false)};
  }
  iterator upper_bound(const Key &key) {
    IndexKeyBytes bytes;
    IndexKey<Key>::append(bytes, key);
    return {this, lookupBound(bytes, true)};
  }
  const_iterator upper_bound(const Key &key) const {
    IndexKeyBytes bytes;
    IndexKey<Key>::append(bytes, key);
    return {this, lookupBound(bytes, true)};
  }
  size_t count(const Key &key) const { return find(key) != end(); }
  Value &at(const Key &key) {
    auto entry = find(key);
    if (entry == end())
      std::abort();
    return entry->second;
  }
  const Value &at(const Key &key) const {
    auto entry = find(key);
    if (entry == end())
      std::abort();
    return entry->second;
  }
  Value &operator[](const Key &key) { return emplace(key).first->second; }
  template <typename K, typename... Args>
  std::pair<iterator, bool> try_emplace(K &&key, Args &&...args) {
    return emplace(std::forward<K>(key), std::forward<Args>(args)...);
  }
  template <typename K, typename... Args>
  std::pair<iterator, bool> emplace(K &&key, Args &&...args) {
    return emplaceWithUndo(nullptr, std::forward<K>(key),
                           std::forward<Args>(args)...);
  }

  /// New leaves are removed unless committed. Existing values retain stable
  /// addresses and are neither copied nor reverted. All insertions into this
  /// map must use this batch while it is alive; values may be published after
  /// every allocation-bearing operation in a larger update has succeeded.
  class InsertionBatch {
    IndexMap *owner;
    std::vector<InsertionUndo> journal;

  public:
    explicit InsertionBatch(IndexMap &owner) : owner(&owner) {}
    InsertionBatch(const InsertionBatch &) = delete;
    InsertionBatch &operator=(const InsertionBatch &) = delete;
    InsertionBatch(InsertionBatch &&other) noexcept
        : owner(std::exchange(other.owner, nullptr)),
          journal(std::move(other.journal)) {}
    ~InsertionBatch() { rollback(); }
    void rollback() noexcept {
      if (!owner)
        return;
      for (auto entry = journal.rbegin(); entry != journal.rend(); ++entry)
        owner->rollbackInsertion(*entry);
      owner = nullptr;
    }
    void commit() noexcept { owner = nullptr; }
    template <typename K, typename... Args>
    std::pair<iterator, bool> emplace(K &&key, Args &&...args) {
      assert(owner);
      return owner->emplaceWithUndo(&journal, std::forward<K>(key),
                                    std::forward<Args>(args)...);
    }
  };

private:
  template <typename K, typename... Args>
  std::pair<iterator, bool> emplaceWithUndo(std::vector<InsertionUndo> *journal,
                                            K &&key, Args &&...args) {
    Key ownedKey(std::forward<K>(key));
    IndexKeyBytes probe;
    IndexKey<Key>::append(probe, ownedKey);
    size_t candidate = root;
    while (candidate != None && nodes[candidate].bit != None &&
           nodes[candidate].bit / 8 < probe.size()) {
      candidate = nodes[candidate].child[branch(probe, nodes[candidate].bit)];
    }
    if (candidate != None && sameBytes(nodes[candidate].first->bytes, probe))
      return {{this, nodes[candidate].first}, false};
    // Only a new leaf owns its encoded key.
    std::vector<uint8_t> bytes(probe.begin(), probe.end());
    size_t differingBit = 0;
    if (candidate != None) {
      const auto &existing = nodes[candidate].first->bytes;
      size_t byte = 0;
      while (byte < bytes.size() && byte < existing.size() &&
             bytes[byte] == existing[byte])
        ++byte;
      // All encodings of a given Key type are prefix free.
      assert(byte < bytes.size() && byte < existing.size());
      unsigned difference = bytes[byte] ^ existing[byte];
      differingBit = byte * 8;
      for (unsigned mask = 128; !(difference & mask); mask >>= 1)
        ++differingBit;
    }
    // Finish all allocations before publishing a leaf. This index also backs
    // persistent runtime caches, which must survive a failed insertion.
    std::vector<size_t> path;
    size_t subtree = root;
    if (candidate != None) {
      while (nodes[subtree].bit != None && nodes[subtree].bit < differingBit) {
        path.push_back(subtree);
        subtree = nodes[subtree].child[branch(bytes, nodes[subtree].bit)];
      }
    }
    size_t required = nodes.size() + (candidate == None ? 1 : 2);
    if (required > nodes.capacity())
      nodes.reserve(
          std::max(required, std::min(nodes.max_size(), nodes.capacity() * 2)));
    // The journal itself can allocate. Record the insertion before creating a
    // leaf; rollback recognizes a record whose mapped-value constructor failed.
    if (journal)
      journal->push_back(
          {root, nodes.size(), entries.size(), subtree, std::move(path)});
    const auto &ancestors = journal ? journal->back().path : path;
    entries.emplace_back(std::move(ownedKey), std::move(bytes),
                         std::forward<Args>(args)...);
    Entry *inserted = &entries.back();
    const size_t leaf = nodes.size();
    nodes.push_back({None, {None, None}, inserted, inserted});
    if (candidate == None) {
      root = leaf;
      first = last = inserted;
      return {{this, inserted}, true};
    }
    const unsigned side = branch(inserted->bytes, differingBit);
    inserted->previous =
        side ? nodes[subtree].last : nodes[subtree].first->previous;
    inserted->next = side ? nodes[subtree].last->next : nodes[subtree].first;
    if (inserted->previous)
      inserted->previous->next = inserted;
    else
      first = inserted;
    if (inserted->next)
      inserted->next->previous = inserted;
    else
      last = inserted;
    Node split;
    split.bit = differingBit;
    split.child[side] = leaf;
    split.child[1 - side] = subtree;
    split.first = side ? nodes[subtree].first : inserted;
    split.last = side ? inserted : nodes[subtree].last;
    const size_t splitIndex = nodes.size();
    nodes.push_back(split);
    if (ancestors.empty())
      root = splitIndex;
    else {
      Node &parent = nodes[ancestors.back()];
      parent.child[branch(inserted->bytes, parent.bit)] = splitIndex;
    }
    for (auto ancestor = ancestors.rbegin(); ancestor != ancestors.rend();
         ++ancestor) {
      Node &node = nodes[*ancestor];
      node.first = nodes[node.child[0]].first;
      node.last = nodes[node.child[1]].last;
    }
    return {{this, inserted}, true};
  }

  void rollbackInsertion(const InsertionUndo &undo) noexcept {
    if (entries.size() == undo.entryCount)
      return;
    assert(entries.size() == undo.entryCount + 1);
    Entry &inserted = entries.back();
    if (inserted.previous)
      inserted.previous->next = inserted.next;
    else
      first = inserted.next;
    if (inserted.next)
      inserted.next->previous = inserted.previous;
    else
      last = inserted.previous;
    root = undo.root;
    if (!undo.path.empty()) {
      Node &parent = nodes[undo.path.back()];
      parent.child[branch(inserted.bytes, parent.bit)] = undo.subtree;
    }
    nodes.resize(undo.nodeCount);
    for (auto ancestor = undo.path.rbegin(); ancestor != undo.path.rend();
         ++ancestor) {
      Node &node = nodes[*ancestor];
      node.first = nodes[node.child[0]].first;
      node.last = nodes[node.child[1]].last;
    }
    entries.pop_back();
  }
};

template <typename Key> class IndexSet {
  IndexMap<Key, uint8_t> values;

public:
  class iterator {
    typename IndexMap<Key, uint8_t>::const_iterator value;

  public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = Key;
    using difference_type = std::ptrdiff_t;
    using pointer = const Key *;
    using reference = const Key &;
    iterator() = default;
    iterator(typename IndexMap<Key, uint8_t>::const_iterator value)
        : value(value) {}
    reference operator*() const { return value->first; }
    pointer operator->() const { return &value->first; }
    iterator &operator++() {
      ++value;
      return *this;
    }
    iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    iterator &operator--() {
      --value;
      return *this;
    }
    iterator operator--(int) {
      auto old = *this;
      --*this;
      return old;
    }
    bool operator==(const iterator &other) const {
      return value == other.value;
    }
    bool operator!=(const iterator &other) const { return !(*this == other); }
  };
  iterator begin() const { return values.begin(); }
  iterator end() const { return values.end(); }
  std::reverse_iterator<iterator> rbegin() const {
    return std::make_reverse_iterator(end());
  }
  std::reverse_iterator<iterator> rend() const {
    return std::make_reverse_iterator(begin());
  }
  size_t size() const { return values.size(); }
  bool empty() const { return values.empty(); }
  size_t count(const Key &key) const { return values.count(key); }
  iterator find(const Key &key) const { return values.find(key); }
  std::pair<iterator, bool> insert(const Key &key) {
    auto result = values.emplace(key);
    return {
        iterator(typename IndexMap<Key, uint8_t>::const_iterator(result.first)),
        result.second};
  }
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args &&...args) {
    return insert(Key(std::forward<Args>(args)...));
  }
};

template <typename T> struct OwnedIndexKey {
  using type = std::decay_t<T>;
};
template <typename... T> struct OwnedIndexKey<std::tuple<T...>> {
  using type = std::tuple<typename OwnedIndexKey<T>::type...>;
};
template <typename A, typename B> struct OwnedIndexKey<std::pair<A, B>> {
  using type = std::pair<typename OwnedIndexKey<A>::type,
                         typename OwnedIndexKey<B>::type>;
};

template <typename T> struct RecordRange {
  std::vector<const T *> rows;
  class iterator {
    typename std::vector<const T *>::const_iterator position;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T *;
    using reference = const T &;
    iterator(typename std::vector<const T *>::const_iterator position)
        : position(position) {}
    reference operator*() const { return **position; }
    pointer operator->() const { return *position; }
    iterator &operator++() {
      ++position;
      return *this;
    }
    iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    bool operator==(const iterator &other) const {
      return position == other.position;
    }
    bool operator!=(const iterator &other) const { return !(*this == other); }
  };
  iterator begin() const { return rows.begin(); }
  iterator end() const { return rows.end(); }
  size_t size() const { return rows.size(); }
  bool empty() const { return rows.empty(); }
  const T &front() const { return *rows.front(); }
};

template <typename T, typename GetKey>
auto groupRecords(const std::vector<T> &rows, GetKey getKey) {
  using Key = typename OwnedIndexKey<decltype(getKey(rows.front()))>::type;
  IndexMap<Key, RecordRange<T>> groups;
  for (const auto &row : rows)
    groups[getKey(row)].rows.push_back(&row);
  return groups;
}

// Single-parent forests need no transitive ancestor tables. Each direct edge
// is visited once; preorder intervals answer subtype queries in fixed time.
class TypeHierarchyIndex {
  struct Interval {
    size_t begin = 0, end = 0;
  };
  IndexMap<uint64_t, Interval> intervals;

public:
  template <typename Rows> bool build(const Rows &rows) {
    intervals = {};
    IndexMap<uint64_t, size_t> ordinals;
    for (size_t i = 0; i != rows.size(); ++i)
      if (!rows[i].id || !ordinals.emplace(rows[i].id, i).second)
        return false;
    std::vector<std::vector<size_t>> children(rows.size());
    std::vector<size_t> roots;
    for (size_t i = 0; i != rows.size(); ++i) {
      if (!rows[i].base) {
        roots.push_back(i);
        continue;
      }
      auto parent = ordinals.find(rows[i].base);
      if (parent == ordinals.end())
        return false;
      children[parent->second].push_back(i);
    }
    struct Frame {
      size_t ordinal, child = 0;
    };
    size_t preorder = 0;
    std::vector<Frame> pending;
    for (size_t root : roots) {
      intervals.emplace(rows[root].id, Interval{preorder++, 0});
      pending.push_back({root});
      while (!pending.empty()) {
        Frame &frame = pending.back();
        if (frame.child != children[frame.ordinal].size()) {
          size_t child = children[frame.ordinal][frame.child++];
          intervals.emplace(rows[child].id, Interval{preorder++, 0});
          pending.push_back({child});
          continue;
        }
        intervals.at(rows[frame.ordinal].id).end = preorder;
        pending.pop_back();
      }
    }
    if (preorder == rows.size())
      return true;
    intervals = {};
    return false;
  }

  bool contains(uint64_t type) const { return intervals.count(type); }
  bool isInstanceOf(uint64_t actual, uint64_t expected) const {
    auto source = intervals.find(actual), target = intervals.find(expected);
    return source != intervals.end() && target != intervals.end() &&
           target->second.begin <= source->second.begin &&
           source->second.begin < target->second.end;
  }
};

template <typename Key, typename T>
const RecordRange<T> &lookupRows(const IndexMap<Key, RecordRange<T>> &groups,
                                 const Key &key) {
  auto found = groups.find(key);
  static const RecordRange<T> empty;
  return found == groups.end() ? empty : found->second;
}

template <typename Iterator, typename GetKey>
void sortByKey(Iterator begin, Iterator end, GetKey getKey) {
  // Physical tables usually arrive in canonical order. An adjacent check
  // charges each variable-width key at most twice and avoids rebuilding an
  // index and moving records on the common path.
  if (std::is_sorted(begin, end, [&](const auto &a, const auto &b) {
        return getKey(a) < getKey(b);
      }))
    return;
  using T = typename std::iterator_traits<Iterator>::value_type;
  using Key = typename OwnedIndexKey<decltype(getKey(*begin))>::type;
  IndexMap<Key, std::vector<size_t>> order;
  size_t index = 0;
  for (auto value = begin; value != end; ++value, ++index)
    order[getKey(*value)].push_back(index);
  std::vector<T> sorted;
  sorted.reserve(index);
  for (const auto &[key, indices] : order)
    for (size_t index : indices)
      sorted.push_back(std::move(begin[index]));
  std::move(sorted.begin(), sorted.end(), begin);
}

} // namespace obelisk::coverage::detail

#endif
