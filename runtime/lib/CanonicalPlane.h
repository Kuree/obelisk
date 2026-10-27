//===- CanonicalPlane.h - Owned or generated state storage -------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_CANONICALPLANE_H
#define TALOS_RUNTIME_LIB_CANONICALPLANE_H

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
  // A known-zero plane retains its logical extent without allocating bytes.
  // Scalar clients may read it as zero; writes cannot create unknown state.
  class Reference {
  public:
    explicit Reference(CanonicalLimb *word) : word(word) {}
    operator uint64_t() const { return word ? *word : 0; }
    Reference &operator=(uint64_t value) {
      if (word)
        *word = value;
      return *this;
    }
    Reference &operator=(const Reference &other) {
      return *this = uint64_t(other);
    }
    Reference &operator|=(uint64_t value) {
      return *this = uint64_t(*this) | value;
    }
    Reference &operator&=(uint64_t value) {
      return *this = uint64_t(*this) & value;
    }
    Reference &operator^=(uint64_t value) {
      return *this = uint64_t(*this) ^ value;
    }

  private:
    CanonicalLimb *word;
  };
  void assignZero(size_t count) {
    bind(nullptr, count);
    shared_ = false;
  }
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
  void swap(CanonicalPlane &other) noexcept {
    owned.swap(other.owned);
    std::swap(words, other.words);
    std::swap(count_, other.count_);
    std::swap(shared_, other.shared_);
  }
  size_t size() const { return count_; }
  bool empty() const { return !count_; }
  CanonicalLimb *data() { return words; }
  const CanonicalLimb *data() const { return words; }
  CanonicalLimb *begin() { return words; }
  CanonicalLimb *end() { return words ? words + count_ : words; }
  const CanonicalLimb *begin() const { return words; }
  const CanonicalLimb *end() const { return words ? words + count_ : words; }
  Reference operator[](size_t index) {
    return Reference(words ? words + index : nullptr);
  }
  uint64_t operator[](size_t index) const { return words ? words[index] : 0; }
  Reference front() { return (*this)[0]; }
  uint64_t front() const { return (*this)[0]; }
  Reference back() { return (*this)[count_ - 1]; }
  uint64_t back() const { return (*this)[count_ - 1]; }

  CanonicalPlane() = default;
  CanonicalPlane(const CanonicalPlane &other) { *this = other; }
  CanonicalPlane &operator=(const CanonicalPlane &other) {
    if (this != &other) {
      if (!other.words) {
        assignZero(other.count_);
        return *this;
      }
      owned.assign(other.begin(), other.end());
      words = owned.data();
      count_ = owned.size();
      shared_ = false;
    }
    return *this;
  }
  bool operator==(const CanonicalPlane &other) const {
    if (size() != other.size())
      return false;
    for (size_t index = 0; index != size(); ++index)
      if ((*this)[index] != other[index])
        return false;
    return true;
  }

private:
  std::vector<uint64_t> owned;
  CanonicalLimb *words = nullptr;
  size_t count_ = 0;
  bool shared_ = false;
};

#endif
