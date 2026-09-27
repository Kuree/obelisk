//===- CanonicalPlane.h - Owned or generated state storage -------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_CANONICALPLANE_H
#define OBELISK_RUNTIME_LIB_CANONICALPLANE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// Generated planes are byte arrays. The alias-qualified limb also permits
// their use by runtime code when the model and runtime are linked with LTO.
#if defined(__GNUC__) || defined(__clang__)
using CanonicalLimb = uint64_t __attribute__((__may_alias__));
#else
using CanonicalLimb = uint64_t;
#endif

class CanonicalPlane {
public:
  void assign(size_t count, uint64_t value) {
    owned.assign(count, value);
    words = owned.data();
    count_ = count;
    shared_ = false;
  }
  void bind(uint8_t *plane, size_t count) {
    std::vector<uint64_t>().swap(owned);
    words = reinterpret_cast<CanonicalLimb *>(plane);
    count_ = count;
    shared_ = true;
  }
  bool shared() const { return shared_; }
  size_t size() const { return count_; }
  bool empty() const { return !count_; }
  CanonicalLimb *data() { return words; }
  const CanonicalLimb *data() const { return words; }
  CanonicalLimb *begin() { return words; }
  CanonicalLimb *end() { return count_ ? words + count_ : words; }
  const CanonicalLimb *begin() const { return words; }
  const CanonicalLimb *end() const { return count_ ? words + count_ : words; }
  CanonicalLimb &operator[](size_t index) { return words[index]; }
  const CanonicalLimb &operator[](size_t index) const { return words[index]; }
  CanonicalLimb &front() { return words[0]; }
  const CanonicalLimb &front() const { return words[0]; }
  CanonicalLimb &back() { return words[count_ - 1]; }
  const CanonicalLimb &back() const { return words[count_ - 1]; }

  CanonicalPlane() = default;
  CanonicalPlane(const CanonicalPlane &other) { *this = other; }
  CanonicalPlane &operator=(const CanonicalPlane &other) {
    if (this != &other) {
      owned.assign(other.begin(), other.end());
      words = owned.data();
      count_ = owned.size();
      shared_ = false;
    }
    return *this;
  }
  bool operator==(const CanonicalPlane &other) const {
    return size() == other.size() &&
           std::equal(begin(), end(), other.begin());
  }

private:
  std::vector<uint64_t> owned;
  CanonicalLimb *words = nullptr;
  size_t count_ = 0;
  bool shared_ = false;
};

#endif
