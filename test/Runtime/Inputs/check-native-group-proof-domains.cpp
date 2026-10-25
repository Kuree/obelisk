#include <cstdint>
#include <cstdio>

extern "C" {
bool __obelisk_eval_group_promotion_ready_v1(uint64_t);
void __obelisk_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __obelisk_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
uint64_t *group_test_unknown();
uint8_t *group_test_latches();
uint8_t *group_test_member_latches();
uint64_t *group_test_pending();
}

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "group proof failure at line %d\n", __LINE__);      \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  auto *unknown = group_test_unknown();
  auto *latches = group_test_latches();
  auto *members = group_test_member_latches();
  auto *pending = group_test_pending();
  auto query = __obelisk_eval_group_promotion_ready_v1;
  auto write = [&](unsigned bit, bool x) {
    uint64_t mask = uint64_t{1} << (bit % 64);
    if (x) {
      unknown[bit / 64] |= mask;
      __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    } else {
      unknown[bit / 64] &= ~mask;
      __obelisk_eval_promotion_recheck_range_v1(bit, 1);
    }
  };
  CHECK(unknown[0] == 2 && unknown[1] == 128);
  CHECK(query(0) && query(1));
  CHECK(latches[0] == 1 && latches[1] == 1);
  CHECK(pending[0] == 1 && pending[1] == 2);
  CHECK(members[0] == 0 && members[65] == 0);

  // Only the first domain contains bit zero. Existing owner proofs stay
  // authoritative; successful group queries do not mark an inactive owner.
  write(0, true);
  CHECK(latches[0] == 0 && latches[1] == 1);
  CHECK(!query(0) && query(1));
  for (unsigned i = 0; i != 20; ++i)
    CHECK(!query(0)); // stable masked-word negative hint
  write(0, false);
  CHECK(latches[0] == 0); // gained knownness never asserts a positive proof
  CHECK(query(0));

  // Clearing a remembered unknown bit must rescan the complete domain. A
  // later range/word may now contain X/Z, including a dependency shared by
  // both domains. No hint may turn into an enduring negative or positive.
  write(9, true);
  CHECK(latches[0] == 0 && latches[1] == 0);
  CHECK(!query(0) && !query(1));
  write(9, false);
  write(70, true);
  CHECK(!query(0) && !query(1));
  write(70, false);
  CHECK(query(0) && query(1));

  // Partial-word neighbors and gaps never enter either certificate.
  write(2, true);
  write(71, false);
  CHECK(latches[0] == 1 && latches[1] == 1);
  CHECK(query(0) && query(1));

  // A full reset clears positives but can retain immutable negative hints.
  // Simulate a reused context whose old hinted word is now known while a
  // different exact range is unknown; the new query must discover it.
  write(0, true);
  CHECK(!query(0));
  latches[0] = latches[1] = 0;
  unknown[0] = 2;
  unknown[1] = 128 | 64;
  CHECK(!query(0) && !query(1));
  write(70, false);
  CHECK(query(0) && query(1));
  CHECK(pending[0] == 1 && pending[1] == 2);
  for (unsigned i = 0; i != 66; ++i)
    CHECK(members[i] == 0);
  CHECK(unknown[0] == 2 && unknown[1] == 128);
}
