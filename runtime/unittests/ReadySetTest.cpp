//===- ReadySetTest.cpp - Ordered ready-set invariants
//---------------------===//

#include "obelisk/Runtime/ReadySet.h"
#include "obelisk/Runtime/ClockKernelReadySet.h"
#include "gtest/gtest.h"

#include <random>
#include <set>
#include <utility>

using obelisk::runtime::CursorReadySet;
using obelisk::runtime::CursorReadySetLayout;
using obelisk::runtime::CursorReadySetView;
using obelisk::runtime::ReadySet;
using obelisk::runtime::ReadySetLayout;
using obelisk::runtime::ReadySetView;

namespace {
constexpr uint32_t none = ReadySetLayout::noBit;

class ReadySetCapacity : public testing::TestWithParam<uint32_t> {};

TEST_P(ReadySetCapacity, ClockKernelPublicationMaintainsGeneratedIndex) {
  if (!GetParam())
    return;
  uint32_t words = (uint64_t{GetParam()} + 63) / 64;
  const ReadySetLayout layout =
      obelisk::runtime::clockKernelReadySetLayout(words);
  std::vector<uint64_t> indexed(layout.storageWords);
  ReadySetView ready(indexed.data(), layout);
  ready.clear();
  obelisk_rt_native_clock_kernel kernel{};
  kernel.ingress_mask = indexed.data();
  kernel.ingress_word_count = words;
  kernel.reserved = obelisk::runtime::indexedClockKernelReadySet;
  // Generated consumption and native publication alternate, including a
  // backwards wakeup and publication after the cached empty sentinel.
  uint32_t last = GetParam() - 1;
  for (unsigned iteration = 0; iteration != 3; ++iteration) {
    obelisk::runtime::publishClockKernelReady(kernel, last);
    obelisk::runtime::publishClockKernelReady(kernel, last);
    EXPECT_EQ(ready.findFirst(), last);
    obelisk::runtime::publishClockKernelReady(kernel, 0);
    EXPECT_EQ(ready.popFirst(), 0u);
    if (last)
      EXPECT_EQ(ready.popFirst(), last);
    EXPECT_EQ(ready.findFirst(), none);
  }
  // Old ABI clients still provide only leaves; no cache/summary may be touched.
  std::vector<uint64_t> raw(words + 1);
  raw.back() = 0xabcde;
  kernel.ingress_mask = raw.data();
  kernel.reserved = 0;
  obelisk::runtime::publishClockKernelReady(kernel, last);
  EXPECT_EQ(raw[last / 64], uint64_t{1} << (last % 64));
  EXPECT_EQ(raw.back(), 0xabcdeu);
}

TEST_P(ReadySetCapacity, EmptyBoundsAndInlineLayout) {
  uint32_t n = GetParam();
  ReadySet ready(n);
  EXPECT_EQ(ready.capacity(), n);
  EXPECT_EQ(ready.findFirst(), none);
  EXPECT_EQ(ready.popFirst(), none);
  EXPECT_EQ(ready.findAtOrAfter(0), none);
  EXPECT_FALSE(ready.set(n));
  EXPECT_FALSE(ready.set(none));
  EXPECT_FALSE(ready.test(n));
  EXPECT_FALSE(ready.reset(n));
  EXPECT_FALSE(ready.setWord(ready.wordCount(), UINT64_MAX));
  if (n <= 64) {
    EXPECT_FALSE(ready.getLayout().hasCache());
    EXPECT_FALSE(ready.getLayout().hasSummaries());
    EXPECT_EQ(ready.getLayout().storageWords, n ? 1u : 0u);
  }
}

TEST_P(ReadySetCapacity, DuplicateActivationAndTailPadding) {
  uint32_t n = GetParam();
  ReadySet ready(n);
  for (uint32_t word = 0; word < ready.wordCount(); ++word) {
    EXPECT_TRUE(ready.setWord(word, UINT64_MAX));
    EXPECT_FALSE(ready.setWord(word, UINT64_MAX));
  }
  for (uint32_t bit = 0; bit < n; ++bit) {
    ASSERT_EQ(ready.findFirst(), bit);
    EXPECT_FALSE(ready.set(bit));
    EXPECT_EQ(ready.popFirst(), bit);
    EXPECT_FALSE(ready.reset(bit));
  }
  EXPECT_EQ(ready.popFirst(), none);
}

TEST_P(ReadySetCapacity, CursorDoesNotHideBackwardWakeups) {
  uint32_t n = GetParam();
  if (n < 2)
    return;
  ReadySet ready(n);
  ready.set(0);
  ready.set(n - 1);
  EXPECT_EQ(ready.findAtOrAfter(1), n - 1);
  EXPECT_EQ(ready.findFirst(), 0u);
  ready.reset(0); // Consume before executing the owner.
  EXPECT_EQ(ready.findFirst(), n - 1);
  ready.set(0); // Its execution reactivates a lower word.
  EXPECT_EQ(ready.popFirst(), 0u);
  EXPECT_EQ(ready.popFirst(), n - 1);
  EXPECT_EQ(ready.popFirst(), none);
  ready.set(n - 1); // Reactivate after the empty sentinel was cached.
  EXPECT_EQ(ready.popFirst(), n - 1);
}

TEST_P(ReadySetCapacity, ClearAndResizeDiscardDerivedState) {
  ReadySet ready(GetParam());
  for (uint32_t bit = 0; bit < ready.capacity(); bit += 63)
    ready.set(bit);
  ready.clear();
  EXPECT_EQ(ready.findFirst(), none);
  for (uint32_t n : {65u, 1u, 0u, GetParam()}) {
    ready.resize(n);
    EXPECT_EQ(ready.findFirst(), none);
    if (n) {
      ready.set(n - 1);
      EXPECT_EQ(ready.popFirst(), n - 1);
    }
  }
}

TEST_P(ReadySetCapacity, DifferentialMutationAndOrderedSelection) {
  uint32_t n = GetParam();
  if (!n)
    return;
  ReadySet ready(n);
  std::set<uint32_t> reference;
  std::mt19937 random(17);
  for (unsigned step = 0; step < 10000; ++step) {
    uint32_t bit = random() % n;
    switch (random() % 5) {
    case 0:
    case 1:
      EXPECT_EQ(ready.set(bit), reference.insert(bit).second);
      break;
    case 2:
      EXPECT_EQ(ready.reset(bit), reference.erase(bit) != 0);
      break;
    case 3: {
      uint32_t expected = reference.empty() ? none : *reference.begin();
      ASSERT_EQ(ready.popFirst(), expected);
      if (expected != none)
        reference.erase(expected);
      break;
    }
    case 4: {
      uint32_t word = bit / 64;
      uint64_t mask = (uint64_t{random()} << 32) | random();
      bool changed = false;
      for (uint32_t i = 0; i < 64 && uint64_t{word} * 64 + i < n; ++i)
        if (mask & (uint64_t{1} << i))
          changed |= reference.erase(word * 64 + i) != 0;
      EXPECT_EQ(ready.clearWord(word, mask), changed);
      break;
    }
    }
    ASSERT_EQ(ready.findFirst(), reference.empty() ? none : *reference.begin());
    auto next = reference.lower_bound(bit);
    ASSERT_EQ(ready.findAtOrAfter(bit), next == reference.end() ? none : *next);
    EXPECT_EQ(ready.test(bit), reference.count(bit) != 0);
  }
}

TEST_P(ReadySetCapacity, RebuildFromAuthoritativeLeaves) {
  ReadySetLayout layout(GetParam());
  std::vector<uint64_t> storage(layout.storageWords, UINT64_MAX);
  ReadySetView ready(storage.data(), layout);
  ready.rebuild();
  for (uint32_t bit = 0; bit < layout.capacity; ++bit)
    ASSERT_EQ(ready.popFirst(), bit);
  EXPECT_EQ(ready.popFirst(), none);
  if (layout.capacity) {
    storage[layout.counts[0] - 1] = UINT64_MAX;
    ready.rebuild();
    EXPECT_EQ(ready.findFirst(), (layout.counts[0] - 1) * 64);
  }
}

TEST_P(ReadySetCapacity, CursorPolicyPreservesMembershipAndRebuild) {
  uint32_t n = GetParam();
  ReadySet minimum(n);
  CursorReadySet cursor(n);
  EXPECT_FALSE(cursor.getLayout().hasCache());
  EXPECT_EQ(cursor.getLayout().hasSummaries(),
            minimum.getLayout().hasSummaries());
  EXPECT_EQ(cursor.getLayout().storageWords + (n > 64 ? 1 : 0),
            minimum.getLayout().storageWords);
  std::mt19937 random(37);
  for (unsigned step = 0; step < 10000; ++step) {
    uint32_t bit = n ? random() % n : 0;
    switch (random() % 6) {
    case 0:
      EXPECT_EQ(cursor.set(bit), minimum.set(bit));
      break;
    case 1:
      EXPECT_EQ(cursor.reset(bit), minimum.reset(bit));
      break;
    case 2:
      ASSERT_EQ(cursor.popFirst(), minimum.popFirst());
      break;
    case 3: {
      uint64_t mask = (uint64_t{random()} << 32) | random();
      EXPECT_EQ(cursor.setWord(bit / 64, mask),
                minimum.setWord(bit / 64, mask));
      break;
    }
    case 4: {
      uint64_t mask = (uint64_t{random()} << 32) | random();
      EXPECT_EQ(cursor.clearWord(bit / 64, mask),
                minimum.clearWord(bit / 64, mask));
      break;
    }
    case 5:
      if (step % 97 == 0) {
        cursor.clear();
        minimum.clear();
      }
      break;
    }
    ASSERT_EQ(cursor.findAtOrAfter(bit), minimum.findAtOrAfter(bit));
    ASSERT_EQ(cursor.findFirst(), minimum.findFirst());
    EXPECT_EQ(cursor.test(bit), minimum.test(bit));
  }
  CursorReadySetLayout layout(n);
  std::vector<uint64_t> storage(layout.storageWords, UINT64_MAX);
  for (uint32_t word = 0; word < cursor.wordCount(); ++word) {
    EXPECT_EQ(cursor.word(word), minimum.word(word));
    storage[word] = cursor.word(word);
  }
  CursorReadySetView rebuilt(storage.data(), layout);
  rebuilt.rebuild();
  while (minimum.findFirst() != none)
    ASSERT_EQ(rebuilt.popFirst(), minimum.popFirst());
  EXPECT_EQ(rebuilt.popFirst(), none);
  auto copy = cursor;
  auto moved = std::move(copy);
  EXPECT_EQ(copy.findFirst(), none);
  EXPECT_EQ(moved.findFirst(), cursor.findFirst());
  cursor.resize(65);
  EXPECT_EQ(cursor.findFirst(), none);
  cursor.set(64);
  EXPECT_EQ(cursor.popFirst(), 64u);
}

TEST(ReadySet, CopyAndMoveRetainIndependentStorage) {
  for (uint32_t n : {1u, 64u, 65u, 4097u, 1048576u}) {
    ReadySet original(n);
    original.set(n - 1);
    ReadySet copy = original;
    original.clear();
    EXPECT_EQ(copy.findFirst(), n - 1);
    ReadySet moved = std::move(copy);
    EXPECT_EQ(moved.popFirst(), n - 1);
    EXPECT_EQ(copy.capacity(), 0u);
    EXPECT_EQ(copy.findFirst(), none);
    EXPECT_FALSE(copy.set(0));
    copy.resize(65);
    copy.set(64);
    EXPECT_EQ(copy.popFirst(), 64u);
    EXPECT_EQ(moved.popFirst(), none);
  }
}

TEST(ReadySet, AssignmentAcrossStorageRepresentations) {
  for (uint32_t from : {0u, 1u, 64u, 65u, 4097u}) {
    for (uint32_t to : {0u, 1u, 64u, 65u, 4097u}) {
      SCOPED_TRACE(testing::Message() << "from=" << from << " to=" << to);
      ReadySet source(from), destination(to);
      if (from)
        source.set(from - 1);
      if (to)
        destination.set(0);
      destination = source;
      source.clear();
      EXPECT_EQ(destination.capacity(), from);
      EXPECT_EQ(destination.findFirst(), from ? from - 1 : none);
      ReadySet &alias = destination;
      destination = alias;
      EXPECT_EQ(destination.findFirst(), from ? from - 1 : none);
      source = std::move(destination);
      EXPECT_EQ(source.findFirst(), from ? from - 1 : none);
      EXPECT_EQ(destination.capacity(), 0u);
      EXPECT_EQ(destination.popFirst(), none);
      destination.clear();
    }
  }
}

TEST(ReadySet, LayoutCoversMaximumCapacityWithoutOverflow) {
  ReadySetLayout layout(UINT32_MAX);
  EXPECT_EQ(layout.counts[0], 67108864u);
  EXPECT_EQ(layout.lastWordMask(), (uint64_t{1} << 63) - 1);
  ASSERT_LE(layout.levels, ReadySetLayout::maxLevels);
  EXPECT_EQ(layout.counts[layout.levels - 1], 1u);
  uint64_t end = layout.counts[0] + 1;
  for (unsigned level = 1; level < layout.levels; ++level) {
    EXPECT_EQ(layout.offsets[level], end);
    EXPECT_EQ(layout.counts[level], (layout.counts[level - 1] + 63) / 64);
    end += layout.counts[level];
  }
  EXPECT_EQ(layout.storageWords, end);
}

TEST_P(ReadySetCapacity, SummaryAndCacheInvariantsAfterBulkMutation) {
  ReadySetLayout layout(GetParam());
  std::vector<uint64_t> storage(layout.storageWords);
  ReadySetView ready(storage.data(), layout);
  ready.clear();
  if (!layout.capacity)
    return;
  std::mt19937 random(29);
  for (unsigned step = 0; step < 1000; ++step) {
    SCOPED_TRACE(step);
    uint32_t word = random() % layout.counts[0];
    uint64_t mask = (uint64_t{random()} << 32) | random();
    if (step % 3)
      ready.setWord(word, mask);
    else
      ready.clearWord(word, mask);
    if (step % 97 == 0)
      ready.clear();
    // Inspect derived state independently, before selection repairs the cache.
    for (unsigned level = 1; level < layout.levels; ++level) {
      for (uint32_t index = 0; index < layout.counts[level]; ++index) {
        uint64_t expected = 0;
        for (uint32_t bit = 0; bit < 64; ++bit) {
          uint32_t child = index * 64 + bit;
          if (child < layout.counts[level - 1] &&
              storage[layout.offsets[level - 1] + child])
            expected |= uint64_t{1} << bit;
        }
        ASSERT_EQ(storage[layout.offsets[level] + index], expected);
      }
    }
    uint32_t firstWord = 0;
    while (firstWord < layout.counts[0] && !storage[firstWord])
      ++firstWord;
    if (layout.hasCache())
      EXPECT_LE(storage[layout.cacheOffset()], firstWord);
    EXPECT_EQ(storage[layout.counts[0] - 1] & ~layout.lastWordMask(), 0u);
    ready.findFirst();
    if (layout.hasCache())
      EXPECT_EQ(storage[layout.cacheOffset()], firstWord);
  }
}

TEST(ReadySet, SummaryChainClearsAndReappears) {
  ReadySet ready(1048576);
  EXPECT_GT(ready.getLayout().levels, 2u);
  for (uint32_t bit : {0u, 63u, 64u, 4095u, 4096u, 262143u, 262144u, 1048575u})
    ready.set(bit);
  for (uint32_t bit : {0u, 63u, 64u, 4095u, 4096u, 262143u, 262144u, 1048575u})
    EXPECT_EQ(ready.popFirst(), bit);
  EXPECT_EQ(ready.popFirst(), none);
  ready.set(1048575);
  ready.set(64);
  EXPECT_EQ(ready.popFirst(), 64u);
  EXPECT_EQ(ready.popFirst(), 1048575u);
}

INSTANTIATE_TEST_SUITE_P(Boundaries, ReadySetCapacity,
                         testing::Values(0u, 1u, 63u, 64u, 65u, 127u, 128u,
                                         129u, 2048u, 2049u, 4095u, 4096u,
                                         4097u, 65536u, 262145u));
} // namespace
